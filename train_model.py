#!/usr/bin/env python3
"""Train an eight-feature logistic reuse predictor using only the Python stdlib."""
import argparse
import csv
import glob
import hashlib
import json
import math
import random
import subprocess
import tempfile
from pathlib import Path

FEATURES = ["pc_repeat_rate","pc_frequency","block_recency","block_frequency",
            "same_block","stable_stride","stride_magnitude","is_write"]


def discover(values):
    paths = []
    for value in values:
        p = Path(value)
        if p.is_dir(): paths.extend(sorted(p.rglob("*.trace")))
        else: paths.extend(Path(item) for item in sorted(glob.glob(value)))
    result = sorted(set(p.resolve() for p in paths))
    if not result: raise ValueError("no matching trace files")
    return result


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""): digest.update(chunk)
    return digest.hexdigest()


def sigmoid(z):
    return 1/(1+math.exp(-z)) if z >= 0 else math.exp(z)/(1+math.exp(z))


def probability(weights,features):
    return sigmoid(weights[0]+sum(w*x for w,x in zip(weights[1:],features)))


def metrics(rows,weights,threshold):
    tp=fp=tn=fn=0
    for features,label in rows:
        predicted = probability(weights,features) >= threshold
        if predicted and label: tp+=1
        elif predicted: fp+=1
        elif label: fn+=1
        else: tn+=1
    precision=tp/(tp+fp) if tp+fp else 0
    recall=tp/(tp+fn) if tp+fn else 0
    return {"samples":len(rows),"positive_fraction":(tp+fn)/len(rows),
            "accuracy":(tp+tn)/len(rows),"precision":precision,"recall":recall,
            "f1":2*precision*recall/(precision+recall) if precision+recall else 0,
            "true_positive":tp,"false_positive":fp,"true_negative":tn,"false_negative":fn}


def collect(paths,args,scratch,role):
    reservoir=[]
    seen=0
    rng=random.Random(args.seed+(role=="validation"))
    for index,path in enumerate(paths):
        dataset=scratch/f"{role}-{index}.csv"
        subprocess.run([str(args.simulator.resolve()),"--trace",str(path),"--block",str(args.block),
                        "--export-dataset",str(dataset),"--horizon",str(args.horizon)],check=True,capture_output=True,text=True)
        with dataset.open(newline="",encoding="utf-8") as f:
            for row in csv.DictReader(f):
                item=([float(row[name]) for name in FEATURES],int(row["label"]))
                seen+=1
                if len(reservoir)<args.max_samples: reservoir.append(item)
                else:
                    slot=rng.randrange(seen)
                    if slot<len(reservoir): reservoir[slot]=item
        print(f"  {role}: {path.name}",flush=True)
    if not reservoir: raise ValueError(f"{role} traces need more than horizon={args.horizon} block accesses")
    if len({label for _,label in reservoir})<2: raise ValueError(f"{role} data must contain both reused and non-reused blocks")
    return reservoir,seen


def train(args):
    training=discover(args.train)
    validation=discover(args.validation)
    training_hashes={fingerprint(p) for p in training}
    validation_hashes={fingerprint(p) for p in validation}
    if training_hashes & validation_hashes:
        raise ValueError("training and validation overlap: use separate program runs/traces")
    # Local scratch avoids dependence on OS temp permissions and stays in project.
    scratch_root=args.output.parent/".training-cache"
    scratch_root.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch_root) as scratch:
        rows,total=collect(training,args,Path(scratch),"train")
        val,val_total=collect(validation,args,Path(scratch),"validation")
    positives=sum(label for _,label in rows)
    positive_weight=len(rows)/(2*positives)
    negative_weight=len(rows)/(2*(len(rows)-positives))
    weights=[0.0]*9
    rng=random.Random(args.seed)
    for epoch in range(args.epochs):
        rng.shuffle(rows)
        rate=args.learning_rate/(1+0.04*epoch)
        for start in range(0,len(rows),256):
            batch=rows[start:start+256]
            gradient=[0.0]*9
            for features,label in batch:
                error=(probability(weights,features)-label)*(positive_weight if label else negative_weight)
                gradient[0]+=error
                for i,value in enumerate(features,1): gradient[i]+=error*value
            for i in range(9):
                weights[i]-=rate*(gradient[i]/len(batch)+(args.l2*weights[i] if i else 0))
        if (epoch+1)%10==0 or epoch+1==args.epochs: print(f"  epoch {epoch+1}/{args.epochs}",flush=True)
    # Choose a classification threshold only on validation traces, never test.
    threshold=max((i/20 for i in range(1,20)),key=lambda t:metrics(val,weights,t)["f1"])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    text="\n".join(["CACHELAB_MODEL 1","schema causal-v1","features 8",f"block_size {args.block}",f"horizon {args.horizon}",
                    f"threshold {threshold:.17g}",f"bias {weights[0]:.17g}",
                    "weights "+" ".join(f"{w:.17g}" for w in weights[1:])])+"\n"
    args.output.write_text(text,encoding="utf-8",newline="\n")
    report={"model":"balanced logistic regression","schema":"causal-v1","features":FEATURES,
            "horizon":args.horizon,"block_size":args.block,"seed":args.seed,"epochs":args.epochs,
            "class_weight":"balanced","l2":args.l2,"learning_rate":args.learning_rate,
            "threshold_selection":"maximum validation F1; not optimized on cache test results",
            "threshold":threshold,"weights":dict(zip(["bias"]+FEATURES,weights)),
            "train":metrics(rows,weights,threshold),"validation":metrics(val,weights,threshold),
            "train_available_samples":total,"validation_available_samples":val_total,
            "train_traces":[{"path":str(p),"sha256":fingerprint(p)} for p in training],
            "validation_traces":[{"path":str(p),"sha256":fingerprint(p)} for p in validation],
            "model_sha256":fingerprint(args.output),
            "limitations":["synthetic training does not establish generalization to real programs",
                            "classification F1 does not establish lower cache misses",
                            "bounded hashed histories may collide; final horizon samples are censored"]}
    metrics_path=args.output.with_suffix(".metrics.json")
    metrics_path.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"Saved {args.output}; validation F1={report['validation']['f1']:.4f}; threshold={threshold:.2f}")
    return report


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--simulator",type=Path,default=Path("./cache_simulator"))
    p.add_argument("--train",nargs="+",required=True)
    p.add_argument("--validation",nargs="+",required=True)
    p.add_argument("--output",type=Path,default=Path("models/reuse.model"))
    p.add_argument("--horizon",type=int,default=256)
    p.add_argument("--block",type=int,default=64)
    p.add_argument("--epochs",type=int,default=35)
    p.add_argument("--max-samples",type=int,default=60000)
    p.add_argument("--seed",type=int,default=42)
    p.add_argument("--learning-rate",type=float,default=0.2)
    p.add_argument("--l2",type=float,default=0.001)
    return p


if __name__=="__main__":
    p=parser(); args=p.parse_args()
    if args.epochs<=0 or args.max_samples<2 or args.horizon<=0 or args.learning_rate<=0 or args.l2<0:
        p.error("invalid training parameters")
    try: train(args)
    except (ValueError,OSError,subprocess.CalledProcessError) as e:
        detail=getattr(e,"stderr",None) or str(e)
        p.exit(1,f"error: {detail}\n")

#!/usr/bin/env python3
"""Regression tests for cache semantics, causal labels and the ML pipeline."""
import argparse
import csv
import functools
import itertools
import json
import math
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parent
SIMULATOR=None
WORKLOAD=None


class SimulatorTests(unittest.TestCase):
    def setUp(self):
        scratch=ROOT/".cache"/"tests"
        scratch.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=scratch)
        self.directory=Path(self.temp.name)

    def tearDown(self): self.temp.cleanup()

    def invoke(self,text,options=(),success=True):
        path=self.directory/"input.trace"
        path.write_text(text,encoding="utf-8")
        result=subprocess.run([str(SIMULATOR),"--trace",str(path),"--json"]+list(options),capture_output=True,text=True)
        if success:
            self.assertEqual(result.returncode,0,result.stderr)
            return json.loads(result.stdout)
        self.assertNotEqual(result.returncode,0)
        self.assertIn("error:",result.stderr)
        return result

    def model(self,bias,weights=None):
        path=self.directory/"test.model"
        weights=weights or [0.0]*8
        path.write_text("CACHELAB_MODEL 1\nschema causal-v1\nfeatures 8\nblock_size 64\nhorizon 4\nthreshold 0.5\nbias "+str(bias)+"\nweights "+" ".join(map(str,weights))+"\n",encoding="utf-8")
        return path

    def test_basic_hits(self):
        r=self.invoke("R 0\nR 4\nR 8\nR 12\n",["--size","16","--block","16","--assoc","1"])
        self.assertEqual((r["hits"],r["misses"],r["evictions"]),(3,1,0))
        self.assertEqual(r["amat_cycles"],26)

    def test_conflict(self):
        r=self.invoke("R 0\nR 64\nR 0\nR 64\n",["--size","64","--block","16","--assoc","1"])
        self.assertEqual((r["misses"],r["evictions"]),(4,3))

    def test_cross_line_writethrough_bytes(self):
        r=self.invoke("W 0x10 0x3c 16\nR 0x10 0x3c 16\n",["--write","wt"])
        self.assertEqual((r["total_accesses"],r["misses"],r["hits"]),(4,2,2))
        self.assertEqual((r["memory_read_bytes"],r["memory_write_bytes"]),(128,16))

    def test_dirty_eviction_and_flush(self):
        r=self.invoke("W 0\nW 64\n",["--size","64","--block","64","--assoc","1","--flush"])
        self.assertEqual((r["write_backs"],r["flush_write_backs"],r["memory_write_bytes"]),(1,1,128))

    def test_no_write_allocate(self):
        r=self.invoke("W 0x10 0 8\nW 0x10 0 8\nR 0\n",["--alloc","nwa"])
        self.assertEqual((r["hits"],r["misses"],r["memory_reads"],r["memory_write_bytes"]),(0,3,1,16))

    def test_warmup(self):
        r=self.invoke("W 0\nR 0\nR 64\n",["--warmup","1","--limit","1"])
        self.assertEqual((r["warmup_accesses"],r["total_accesses"],r["hits"],r["memory_reads"]),(1,1,1,0))

    def test_warmup_dirty_cost_in_measured_interval(self):
        r=self.invoke("W 0\nR 64\n",["--size","64","--assoc","1","--warmup","1"])
        self.assertEqual((r["total_accesses"],r["write_backs"],r["memory_write_bytes"]),(1,1,64))

    def test_limit_all_policies(self):
        for policy in ("lru","optimal"):
            r=self.invoke("R 0\nR 64\nR 128\nR 0\n",["--policy",policy,"--limit","2"])
            self.assertEqual((r["total_accesses"],r["misses"]),(2,2))

    def test_limit_does_not_parse_unused_suffix(self):
        for policy in ("lru","optimal"):
            r=self.invoke("R 0\nmalformed unused suffix\n",["--policy",policy,"--limit","1"])
            self.assertEqual(r["total_accesses"],1)

    def test_empty_comments(self):
        r=self.invoke(" # comment\n\n")
        self.assertEqual(r["total_accesses"],0)
        self.assertEqual(r["amat_cycles"],0)

    def test_invalid_inputs(self):
        for text in ("R nope\n","X 4\n","R -1\n","R 1 2 0\n","R 1 0xffffffffffffffff 2\n","R 1 2 3 extra\n"):
            with self.subTest(text=text): self.invoke(text,success=False)
        for options in (["--size","0"],["--block","3"],["--assoc","0"],["--assoc","1024"],
                        ["--policy","oops"],["--unknown"],["--write","bad"],["--miss-penalty","nan"],
                        ["--policy","ml"],["--policy","optimal","--alloc","nwa"]):
            with self.subTest(options=options): self.invoke("R 0\n",options,False)

    def test_lru_and_lip(self):
        text="R 0\nR 64\nR 0\nR 128\nR 192\nR 0\n"
        options=["--size","128","--assoc","2"]
        lru=self.invoke(text,options)
        lip=self.invoke(text,options+["--policy","lip"])
        self.assertEqual((lru["hits"],lip["hits"]),(1,2))

    def test_igbi_scan_protects_hot_line_after_training(self):
        lines=[]
        for cycle in range(8):
            lines.extend(["R 0x100 0","R 0x100 0"])
            lines.extend(f"R 0x200 {64*(1+cycle*12+i)}" for i in range(12))
        options=["--size","256","--assoc","4"]
        text="\n".join(lines)+"\n"
        lru=self.invoke(text,options)
        igbi=self.invoke(text,options+["--policy","igbi"])
        self.assertGreater(igbi["hits"],lru["hits"])

    def test_random_seed_repeatability(self):
        text="\n".join(f"R {(i%9)*64}" for i in range(200))+"\n"
        opts=["--policy","random","--size","256","--assoc","4","--seed","29"]
        a=self.invoke(text,opts); b=self.invoke(text,opts)
        for key in ("hits","misses","evictions","memory_traffic_bytes"): self.assertEqual(a[key],b[key])

    def test_all_baseline_accounting(self):
        rng=random.Random(17)
        text="\n".join(f"{'W' if i%3==0 else 'R'} {rng.randrange(20)*64}" for i in range(100))+"\n"
        for policy in ("lru","fifo","random","lfu","mru","optimal","igbi","lip","srrip"):
            r=self.invoke(text,["--policy",policy,"--size","512","--assoc","4"])
            self.assertEqual(r["hits"]+r["misses"],100)
            self.assertEqual(r["read_misses"]+r["write_misses"],r["misses"])
            self.assertEqual(r["memory_reads"],r["misses"])

    def test_optimal_matches_independent_dynamic_program(self):
        def optimum(sequence,capacity):
            @functools.lru_cache(None)
            def solve(i,resident):
                if i==len(sequence): return 0
                block=sequence[i]
                if block in resident: return solve(i+1,resident)
                if len(resident)<capacity: return 1+solve(i+1,tuple(sorted(resident+(block,))))
                return 1+min(solve(i+1,tuple(sorted(tuple(x for x in resident if x!=old)+(block,)))) for old in resident)
            return solve(0,())
        rng=random.Random(7)
        for _ in range(16):
            sequence=tuple(rng.randrange(5) for _ in range(14))
            r=self.invoke("\n".join(f"R {b*64}" for b in sequence),["--policy","optimal","--size","128","--assoc","2"])
            self.assertEqual(r["misses"],optimum(sequence,2))

    def test_dataset_exact_labels_censor_tail_and_causal_features(self):
        sequence=[0,1,2,0,3,1,0,4]
        trace=self.directory/"labels.trace"
        trace.write_text("\n".join(f"R 0x100 {b*64} 8" for b in sequence),encoding="utf-8")
        output=self.directory/"labels.csv"
        subprocess.run([str(SIMULATOR),"--trace",str(trace),"--export-dataset",str(output),"--horizon","3"],check=True,capture_output=True)
        with output.open() as f: rows=list(csv.DictReader(f))
        self.assertEqual(len(rows),len(sequence)-3)
        self.assertEqual([int(r["label"]) for r in rows],[int(sequence[i] in sequence[i+1:i+4]) for i in range(5)])
        # Same past, different future: features remain identical even if labels differ.
        first=rows[0].copy()
        trace.write_text("R 0x100 0 8\nR 0x100 64 8\nR 0x100 128 8\nR 0x100 192 8\n",encoding="utf-8")
        subprocess.run([str(SIMULATOR),"--trace",str(trace),"--export-dataset",str(output),"--horizon","3"],check=True,capture_output=True)
        with output.open() as f: changed=next(csv.DictReader(f))
        for key in first:
            if key!="label": self.assertEqual(first[key],changed[key])
        self.assertNotEqual(first["label"],changed["label"])

    def test_ml_uses_model_and_matches_insertion_extremes(self):
        text="R 0\nR 64\nR 0\nR 128\nR 192\nR 0\n"
        options=["--size","128","--assoc","2"]
        high=self.model(100)
        result=self.invoke(text,options+["--policy","ml","--model",str(high)])
        self.assertEqual(result["hits"],self.invoke(text,options)["hits"])
        low=self.model(-100)
        result=self.invoke(text,options+["--policy","ml","--model",str(low)])
        self.assertEqual(result["hits"],self.invoke(text,options+["--policy","lip"])["hits"])
        self.assertEqual(result["ml_low_priority_insertions"],result["misses"])
        self.assertEqual(result["ml_predictions"],result["total_accesses"])

    def test_cpp_python_model_predictions_agree(self):
        from train_model import probability
        weights=[0.1,0.2,-0.3,0.4,0.5,-0.6,0.7,-0.8,0.9]
        model=self.model(weights[0],weights[1:])
        text="\n".join(f"R 0x100 {b*64}" for b in [0,1,0,2,3,0,4,5,0,6])+"\n"
        trace=self.directory/"parity.trace"; trace.write_text(text,encoding="utf-8")
        data=self.directory/"parity.csv"
        subprocess.run([str(SIMULATOR),"--trace",str(trace),"--export-dataset",str(data),"--horizon","4"],check=True,capture_output=True)
        from train_model import FEATURES
        with data.open() as f: features=[[float(row[n]) for n in FEATURES] for row in csv.DictReader(f)]
        # One-line cache ensures each fresh block needs an insertion. Reproduce labels irrelevant to this assertion.
        expected=sum(probability(weights,x)<0.5 for x in features)
        result=self.invoke("\n".join(text.splitlines()[:len(features)]),["--size","64","--assoc","1","--policy","ml","--model",str(model)])
        self.assertEqual(result["ml_low_priority_insertions"],expected)

    def test_bad_model_rejected(self):
        model=self.model(0)
        model.write_text("CACHELAB_MODEL 99\n",encoding="utf-8")
        self.invoke("R 0\n",["--policy","ml","--model",str(model)],False)

    def test_model_block_size_mismatch(self):
        model=self.model(0)
        self.invoke("R 0\n",["--block","128","--policy","ml","--model",str(model)],False)

    def test_training_and_overlap_guard(self):
        from train_model import parser,train
        train_path=self.directory/"train.trace"
        val_path=self.directory/"validation.trace"
        train_path.write_text("\n".join(f"R 0x100 {(i%3 if i%2 else i)*64}" for i in range(80)),encoding="utf-8")
        val_path.write_text("\n".join(f"R 0x200 {(i%4 if i%2 else i+100)*64}" for i in range(80)),encoding="utf-8")
        args=parser().parse_args(["--simulator",str(SIMULATOR),"--train",str(train_path),"--validation",str(val_path),
                                 "--output",str(self.directory/"learned.model"),"--horizon","4","--epochs","2"])
        report=train(args)
        self.assertEqual(report["train"]["samples"],76)
        self.invoke("R 0\nR 0\n",["--policy","ml","--model",str(args.output)])
        from benchmark import parser as bench_parser,run
        bench=bench_parser().parse_args([str(SIMULATOR),str(train_path),"--model",str(args.output)])
        with self.assertRaisesRegex(ValueError,"overlap"): run(bench)
        args.validation=args.train
        with self.assertRaisesRegex(ValueError,"overlap"): train(args)

    def test_matrix_workloads_compute_same_answer(self):
        if not WORKLOAD: self.skipTest("workload executable not supplied")
        answers=[]
        for mode in ("matrix","tiled"):
            r=subprocess.run([str(WORKLOAD),mode,"16"],check=True,capture_output=True,text=True)
            answers.append(float(next(t.split("=",1)[1] for t in r.stdout.split() if t.startswith("checksum="))))
        self.assertTrue(math.isclose(*answers,rel_tol=1e-10))


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("simulator",type=Path)
    p.add_argument("--workload",type=Path)
    args=p.parse_args()
    SIMULATOR=args.simulator.resolve()
    WORKLOAD=args.workload.resolve() if args.workload else None
    unittest.main(argv=[sys.argv[0]],verbosity=2)

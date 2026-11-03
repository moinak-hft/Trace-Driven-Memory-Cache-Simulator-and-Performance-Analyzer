#!/usr/bin/env python3
"""Run held-out cache experiments and write CSV, JSON and an offline HTML report."""
import argparse
import csv
import html
import json
import subprocess
from pathlib import Path
from train_model import discover, fingerprint


def write_report(rows,path):
    comparisons=[]
    baselines={(r["trace"],r["cache_size"],r["associativity"],r["seed"]):r for r in rows if r["policy"]=="lru"}
    for r in rows:
        if r["policy"]!="ml": continue
        base=baselines.get((r["trace"],r["cache_size"],r["associativity"],r["seed"]))
        if base and base["misses"]:
            comparisons.append((r,100*(base["misses"]-r["misses"])/base["misses"]))
    mean=sum(d for _,d in comparisons)/len(comparisons) if comparisons else None
    bars=[]
    for row,delta in comparisons:
        label=f"{row['trace']} / {row['cache_size']//1024} KiB / {row['associativity']}-way"
        bars.append(f'<div class="comparison"><span>{html.escape(label)}</span><div class="track"><i class="{"positive" if delta>=0 else "negative"}" style="width:{min(100,abs(delta)):.2f}%"></i></div><b>{delta:+.2f}%</b></div>')
    records=[]
    for r in rows:
        records.append('<tr>'+''.join([
            f'<td>{html.escape(r["trace"])}</td>',f'<td><span class="badge">{r["policy"].upper()}</span></td>',
            f'<td>{r["cache_size"]//1024}</td>',f'<td>{r["associativity"]}</td>',f'<td>{r["seed"]}</td>',
            f'<td>{r["misses"]:,}</td>',f'<td>{r["miss_rate"]*100:.3f}%</td>',
            f'<td>{r["amat_cycles"]:.3f}</td>',f'<td>{r["memory_traffic_bytes"]/1024:.1f}</td>',
            f'<td>{r["simulation_seconds"]:.4f}</td>'
        ])+'</tr>')
    headline=f"{mean:+.2f}%" if mean is not None else "N/A"
    template='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CacheLab experiment report</title><style>
:root{font-family:Inter,Segoe UI,Arial,sans-serif;color:#193141;background:#f2f5f7}body{max-width:1240px;margin:auto;padding:44px 24px}header{border-top:5px solid #127c78;padding-top:24px}h1{font-size:42px;letter-spacing:-1.5px;margin:8px 0}h2{font-size:22px;margin-top:34px}.eyebrow{font-size:12px;letter-spacing:2px;font-weight:700;color:#127c78}p{line-height:1.65;color:#526674}.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin:30px 0}.card{background:white;border:1px solid #dce5e9;border-radius:12px;padding:22px}.card strong{display:block;font-size:30px;color:#163647}.card span{font-size:13px;color:#617380}.panel{background:white;border:1px solid #dce5e9;border-radius:12px;padding:24px}.comparison{display:grid;grid-template-columns:320px 1fr 88px;gap:14px;align-items:center;font-size:12px;margin:13px 0}.comparison b{text-align:right}.track{height:10px;background:#eaf0f3;border-radius:8px;overflow:hidden}.track i{height:100%;display:block}.positive{background:#168a7b}.negative{background:#c66b49}.table-wrap{overflow:auto}table{width:100%;border-collapse:collapse;font-size:12px}th{text-align:left;color:#526674;background:#edf3f5;white-space:nowrap}td,th{padding:12px;border-bottom:1px solid #e4ebee}td:not(:first-child){font-variant-numeric:tabular-nums}.badge{background:#e7f1f0;color:#166762;padding:4px 7px;border-radius:5px;font-weight:700}input{padding:12px 15px;border:1px solid #c8d6dd;border-radius:8px;width:min(400px,90%);margin:0 0 18px}footer{font-size:12px;margin-top:28px;color:#637884}@media(max-width:760px){.cards{grid-template-columns:1fr}.comparison{grid-template-columns:1fr 100px 65px}h1{font-size:32px}}
</style><header><div class="eyebrow">CACHELAB / REPRODUCIBLE EXPERIMENTS</div><h1>Learned reuse. Measured tradeoffs.</h1><p>Trace-driven data cache analysis across workloads, cache configurations and replacement policies.</p></header>
<div class="cards"><div class="card"><strong>__RUNS__</strong><span>Completed simulation runs</span></div><div class="card"><strong>__TRACES__</strong><span>Evaluation traces</span></div><div class="card"><strong>__MEAN__</strong><span>Mean ML miss reduction versus LRU</span></div></div>
<div class="panel"><h2 style="margin-top:0">ML versus LRU</h2><p>Positive values mean fewer misses; negative values mean a regression. Each bar pairs the same trace, cache and seed. The headline is the unweighted arithmetic mean of paired reductions with nonzero LRU misses.</p>__BARS__</div>
<h2>All experiment results</h2><div class="panel"><input id="search" aria-label="Filter results" placeholder="Filter by workload or policy"><div class="table-wrap"><table><thead><tr><th>Trace</th><th>Policy</th><th>KiB</th><th>Ways</th><th>Seed</th><th>Misses</th><th>Miss rate</th><th>AMAT cycles</th><th>Traffic KiB</th><th>Sim seconds</th></tr></thead><tbody>__ROWS__</tbody></table></div></div>
<footer>AMAT = hit latency + miss rate × miss penalty. This is a first-order estimate, not measured program runtime. Simulation seconds include parsing and policy work; OPT also includes future-trace preprocessing. Memory traffic excludes a final dirty flush unless explicitly requested. Generated demo traces model logical references and are synthetic. File-hash separation checks cannot prove independent program runs. See manifest.json for provenance and exact commands. No external scripts, fonts, or network access are required.</footer>
<script>document.getElementById('search').addEventListener('input',function(){const q=this.value.toLowerCase();document.querySelectorAll('tbody tr').forEach(r=>r.hidden=!r.textContent.toLowerCase().includes(q));});</script></html>'''
    for key,value in {"__RUNS__":str(len(rows)),"__TRACES__":str(len({r['trace'] for r in rows})),"__MEAN__":headline,
                      "__BARS__":"".join(bars) or "<p>No paired ML/LRU comparisons.</p>","__ROWS__":"".join(records)}.items():
        template=template.replace(key,value)
    path.write_text(template,encoding="utf-8")


def run(args):
    paths=discover(args.traces)
    policies=args.policies or (["lru","igbi","lip","srrip","ml","optimal"] if args.model else ["lru","fifo","random","lfu","mru","optimal","igbi"])
    model_manifest=None
    if "ml" in policies:
        if not args.model: raise ValueError("ML experiments require --model")
        metadata=args.model.with_suffix(".metrics.json")
        if not metadata.exists(): raise ValueError("model provenance missing: train with train_model.py first")
        model_manifest=json.loads(metadata.read_text(encoding="utf-8"))
        if fingerprint(args.model)!=model_manifest["model_sha256"]: raise ValueError("model differs from its training manifest")
        if args.block!=model_manifest["block_size"]: raise ValueError("block size differs from model training; retrain for this block size")
        known={r["sha256"] for group in ("train_traces","validation_traces") for r in model_manifest[group]}
        if any(fingerprint(p) in known for p in paths): raise ValueError("evaluation trace overlaps model training/validation data")
    rows=[]; commands=[]
    for path in paths:
        for size in args.sizes:
            for assoc in args.assocs:
                if size<args.block*assoc: continue
                for policy in policies:
                    seeds=args.seeds if policy=="random" else args.seeds[:1]
                    for seed in seeds:
                        cmd=[str(args.simulator.resolve()),"--trace",str(path),"--size",str(size),"--block",str(args.block),
                             "--assoc",str(assoc),"--policy",policy,"--seed",str(seed),"--warmup",str(args.warmup),
                             "--hit-cycles",str(args.hit_cycles),"--miss-penalty",str(args.miss_penalty),"--json"]
                        if policy=="ml": cmd.extend(["--model",str(args.model.resolve())])
                        result=subprocess.run(cmd,capture_output=True,text=True,check=True)
                        row=json.loads(result.stdout)
                        row["trace"]=str(path.relative_to(Path.cwd())) if path.is_relative_to(Path.cwd()) else str(path)
                        row["trace_sha256"]=fingerprint(path)
                        rows.append(row); commands.append(cmd)
        print(f"Benchmarked {path.name}",flush=True)
    if not rows: raise ValueError("no valid experiments; check sizes/associativity")
    args.output.mkdir(parents=True,exist_ok=True)
    with (args.output/"results.csv").open("w",newline="",encoding="utf-8") as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    (args.output/"results.json").write_text(json.dumps(rows,indent=2)+"\n",encoding="utf-8")
    manifest={"simulator_sha256":fingerprint(args.simulator),"model":str(args.model) if args.model else None,
              "model_sha256":fingerprint(args.model) if args.model else None,"model_training":model_manifest,
              "traces":[{"path":str(p),"sha256":fingerprint(p)} for p in paths],"commands":commands,
              "evaluation":"closed-loop policy simulation on supplied traces; exact training/validation file hashes rejected for ML",
              "timing":"single-run simulator wall time, not native program runtime or a statistical overhead estimate"}
    (args.output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    write_report(rows,args.output/"report.html")
    print(f"Saved {len(rows)} experiments: {args.output/'report.html'}")
    return rows


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("simulator",type=Path)
    p.add_argument("traces",nargs="+")
    p.add_argument("--model",type=Path)
    p.add_argument("--sizes",nargs="+",type=int,default=[4096,16384])
    p.add_argument("--assocs",nargs="+",type=int,default=[4])
    p.add_argument("--block",type=int,default=64)
    p.add_argument("--warmup",type=int,default=256)
    p.add_argument("--seeds",nargs="+",type=int,default=[1337,2027,991])
    p.add_argument("--policies",nargs="+",choices=["lru","fifo","random","lfu","mru","optimal","igbi","lip","srrip","ml"])
    p.add_argument("--hit-cycles",type=float,default=1)
    p.add_argument("--miss-penalty",type=float,default=100)
    p.add_argument("--output",type=Path,default=Path("results/latest"))
    return p


if __name__=="__main__":
    p=parser(); args=p.parse_args()
    try: run(args)
    except (ValueError,OSError,subprocess.CalledProcessError) as e:
        p.exit(1,f"error: {getattr(e,'stderr',None) or str(e)}\n")

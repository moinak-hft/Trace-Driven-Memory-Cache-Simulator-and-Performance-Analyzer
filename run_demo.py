#!/usr/bin/env python3
"""Generate separate trace splits, train a real model, and evaluate held-out traces."""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parent


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--simulator",type=Path,default=ROOT/"cache_simulator")
    p.add_argument("--quick",action="store_true",help="smaller smoke experiment; not final research evidence")
    args=p.parse_args()
    simulator=args.simulator.resolve()
    if not simulator.is_file(): p.error("build the simulator first: make")
    commands=[
        ["generate_workloads.py","--output","traces","--accesses","4000" if args.quick else "12000","--matrix-n","12" if args.quick else "20"],
        ["train_model.py","--simulator",str(simulator),"--train","traces/train","--validation","traces/validation",
         "--output","models/reuse.model","--epochs","15" if args.quick else "35"],
        ["benchmark.py",str(simulator),"traces/test","--model","models/reuse.model","--sizes","4096","16384","--output","results/demo"]]
    for command in commands:
        subprocess.run([sys.executable]+command,cwd=ROOT,check=True)
    print("Open results/demo/report.html in a browser. Demo traces are synthetic; capture real workloads with Intel Pin next.")


if __name__=="__main__": main()

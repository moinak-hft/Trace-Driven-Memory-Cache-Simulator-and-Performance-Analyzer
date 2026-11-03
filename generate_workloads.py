#!/usr/bin/env python3
"""Generate reproducible, explicitly synthetic memory-access traces."""
import argparse
import json
import random
from pathlib import Path


def stream(count, rng):
    start = rng.randrange(0x10000, 0x20000) * 64
    for i in range(count):
        yield "R", 0x1010, start + 64*i, 8


def hot_scan(count, rng):
    hot = rng.randrange(0x100, 0x200)*64
    scan = 0x800000 + rng.randrange(100)*64
    step = 0
    while step < count:
        for _ in range(3):
            for i in range(24):
                if step >= count: return
                yield "R", 0x2010, hot+i*64, 8
                step += 1
        for _ in range(128):
            if step >= count: return
            yield "R", 0x2020, scan, 8
            scan += 64
            step += 1


def pointer(count, rng):
    order = list(range(512))
    rng.shuffle(order)
    for i in range(count):
        yield "R", 0x3010, 0x200000+order[i % len(order)]*64, 8


def random_access(count, rng):
    for _ in range(count):
        yield "W" if rng.random() < 0.25 else "R", 0x4010, 0x400000+rng.randrange(2048)*64, 8


def phased(count, rng):
    for i in range(count):
        working = 16 if (i//1000) % 2 == 0 else 1024
        yield "R", 0x5010, 0x500000+rng.randrange(working)*64, 8


def matrix(n, tile=0):
    # Logical data references for C += A*B; these are not instrumented CPU traces.
    # The matching C++ workload produces actual program traces under Pin.
    def inner(i, j, k):
        yield "R", 0x6010, 0x100000+(i*n+k)*8, 8
        yield "R", 0x6020, 0x200000+(k*n+j)*8, 8
        yield "R", 0x6030, 0x300000+(i*n+j)*8, 8
        yield "W", 0x6040, 0x300000+(i*n+j)*8, 8
    if tile:
        for ii in range(0,n,tile):
            for kk in range(0,n,tile):
                for jj in range(0,n,tile):
                    for i in range(ii,min(ii+tile,n)):
                        for k in range(kk,min(kk+tile,n)):
                            for j in range(jj,min(jj+tile,n)):
                                yield from inner(i,j,k)
    else:
        for i in range(n):
            for j in range(n):
                for k in range(n):
                    yield from inner(i,j,k)


def generate(output, count=12000, matrix_n=20):
    manifest = {"trace_kind": "synthetic logical references, not Intel Pin captures", "splits": {}}
    functions = {"stream": stream, "hot_scan": hot_scan, "pointer": pointer,
                 "random": random_access, "phases": phased}
    for split, seed in (("train",101),("validation",202),("test",303)):
        directory = output/split
        directory.mkdir(parents=True,exist_ok=True)
        names = []
        for index,(name, function) in enumerate(functions.items()):
            path = directory/f"{name}.trace"
            with path.open("w",encoding="utf-8",newline="\n") as f:
                f.write(f"# synthetic {name}; split={split}; seed={seed+index}\n")
                for typ,pc,address,size in function(count,random.Random(seed+index)):
                    f.write(f"{typ} {pc:#x} {address:#x} {size}\n")
            names.append(path.name)
        # Different dimensions keep validation/test computations distinct.
        n = matrix_n + {"train":0,"validation":2,"test":4}[split]
        for name,tile in (("matrix_naive",0),("matrix_tiled",8)):
            path = directory/f"{name}.trace"
            with path.open("w",encoding="utf-8",newline="\n") as f:
                f.write(f"# synthetic {name}; n={n}; split={split}\n")
                for typ,pc,address,size in matrix(n,tile):
                    f.write(f"{typ} {pc:#x} {address:#x} {size}\n")
            names.append(path.name)
        manifest["splits"][split] = {"seed":seed,"matrix_n":n,"files":names}
    (output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"Generated train/validation/test traces in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=Path("traces"))
    parser.add_argument("--accesses",type=int,default=12000)
    parser.add_argument("--matrix-n",type=int,default=20)
    args = parser.parse_args()
    if args.accesses < 512 or not 4 <= args.matrix_n <= 128:
        parser.error("accesses must be >=512 and matrix-n must be 4..128")
    generate(args.output,args.accesses,args.matrix_n)

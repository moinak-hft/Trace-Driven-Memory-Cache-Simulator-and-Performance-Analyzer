import subprocess
import re
import sys


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 benchmark.py <path_to_simulator> <path_to_trace>")
        sys.exit(1)

    sPath = sys.argv[1]
    trcPath = sys.argv[2]

    policies = ["lru", "fifo", "random", "lfu", "mru", "optimal", "igbi"]

    print(f"{'POLICY':<10} | {'HIT RATE (%)':<15} | {'MISS RATE (%)':<15} | {'EVICTIONS':<10} | {'WRITE BACKS'}")
    print("-" * 75)

    maxiPolicy = ""
    maxiHit = -1.0

    for p in policies:
        cmd = [
            sPath,
            "--size", "65536",
            "--block", "128",
            "--assoc", "16",
            "--policy", p,
            "--write", "wb",
            "--trace", trcPath
        ]

        res = subprocess.run(cmd, capture_output=True, text=True)

        if res.returncode != 0:
            print(f"{p.upper():<10} | FAILED TO RUN")
            continue

        out = res.stdout

        try:
            hit = float(re.search(r"Hit Rate:\s*([0-9.]+)", out).group(1))
            miss = float(re.search(r"Miss Rate:\s*([0-9.]+)", out).group(1))
            evic = int(re.search(r"Evictions:\s*([0-9]+)", out).group(1))
            wb = int(re.search(r"Write Backs:\s*([0-9]+)", out).group(1))

            print(f"{p.upper():<10} | {hit:<15.2f} | {miss:<15.2f} | {evic:<10} | {wb}")

            if p != "optimal" and hit > maxiHit:
                maxiHit = hit
                maxiPolicy = p.upper()

        except AttributeError:
            print(f"{p.upper():<10} | PARSE ERROR")

    print("-" * 75)
    print(f"Result: The best performing realistic algorithm is {maxiPolicy} with a Hit Rate of {maxiHit:.2f}%.")


if __name__ == "__main__":
    main()

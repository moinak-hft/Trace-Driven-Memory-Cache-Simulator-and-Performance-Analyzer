import re
import math
import subprocess
import tempfile
from pathlib import Path
import sys

def run_case(exe, trace_lines, args):
    with tempfile.TemporaryDirectory() as td:
        trace_path = Path(td) / "trace.txt"
        trace_path.write_text("\n".join(trace_lines) + "\n")
        cmd = [exe] + args + [str(trace_path)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(res.stderr.strip() or res.stdout.strip())
        return res.stdout

def get_value(text, key):
    m = re.search(rf"{re.escape(key)}:\s*([0-9]+(?:\.[0-9]+)?)", text)
    if not m:
        raise ValueError(f"Missing {key}")
    v = m.group(1)
    return float(v) if "." in v else int(v)

def check_int(text, key, expected):
    got = int(get_value(text, key))
    assert got == expected, f"{key}: expected {expected}, got {got}"

def check_float(text, key, expected, eps=0.01):
    got = float(get_value(text, key))
    assert math.isclose(got, expected, abs_tol=eps), f"{key}: expected {expected}, got {got}"

def main():
    if len(sys.argv) < 2:
        print("usage: python test_driver.py ./cache_simulator")
        sys.exit(1)

    exe = sys.argv[1]

    out1 = run_case(
        exe,
        ["R 0x0", "R 0x4", "R 0x8", "R 0xc"],
        ["--size", "16", "--block", "16", "--assoc", "1", "--policy", "lru", "--write", "wb", "--alloc", "wa"]
    )
    check_int(out1, "Hits", 3)
    check_int(out1, "Misses", 1)
    check_int(out1, "Evictions", 0)
    check_float(out1, "Hit Rate", 75.00)

    out2 = run_case(
        exe,
        ["R 0x0", "R 0x40", "R 0x0", "R 0x40"],
        ["--size", "64", "--block", "16", "--assoc", "1", "--policy", "lru", "--write", "wb", "--alloc", "wa"]
    )
    check_int(out2, "Hits", 0)
    check_int(out2, "Misses", 4)
    check_int(out2, "Evictions", 3)
    check_float(out2, "Hit Rate", 0.00)

    out3 = run_case(
        exe,
        ["W 0x0", "W 0x0"],
        ["--size", "16", "--block", "16", "--assoc", "1", "--policy", "fifo", "--write", "wt", "--alloc", "wa"]
    )
    check_int(out3, "Hits", 1)
    check_int(out3, "Misses", 1)
    check_float(out3, "Hit Rate", 50.00)

    print("All tests passed")

if __name__ == "__main__":
    main()

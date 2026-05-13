import re
import math
import subprocess
import tempfile
import sys
import argparse
from pathlib import Path

def solve(execPath, trcLines, optSimul):
    with tempfile.TemporaryDirectory() as temp_dir:
        trcFile = Path(temp_dir) / "trace.txt"
        trcFile.write_text("\n".join(trcLines) + "\n")
        
        cmd = [execPath] + optSimul + [str(trcFile)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        
        if res.returncode != 0:
            err = res.stderr.strip() or res.stdout.strip()
            raise RuntimeError(f"Simulation failed: {err}")
            
        return res.stdout

def parser(resText, check):
    patern = rf"{re.escape(check)}:\s*([0-9]+(?:\.[0-9]+)?)"
    match = re.search(patern, resText)
    
    if not match:
        raise ValueError(f"Metric '{check}' not found in simull output")
        
    valStr = match.group(1)
    return float(valStr) if "." in valStr else int(valStr)

def chkInt(resText, check, expVal):
    truVal = int(parser(resText, check))
    assert truVal == expVal, f"{check} mismatch: expected {expVal}, got {truVal}"

def chkFloat(resText, check, expVal, tolerance=0.01):
    truVal = float(parser(resText, check))
    assert math.isclose(truVal, expVal, abs_tol=tolerance), f"{check} mismatch: expected {expVal}, got {truVal}"

def main():
    parserArg = argparse.ArgumentParser(description="Test driver for the cache simull.")
    parserArg.add_argument("simull", help="Path to the cache simull executable")
    args = parserArg.parse_args()

    simull = args.simull

    print("Running Test 1: Basic Hit/Miss Ratio")
    opt1 = solve(
        simull,
        ["R 0x0", "R 0x4", "R 0x8", "R 0xc"],
        ["--size", "16", "--block", "16", "--assoc", "1", "--policy", "lru", "--write", "wb", "--alloc", "wa"]
    )
    chkFloat(opt1, "Hit Rate", 75.00)
    chkFloat(opt1, "Miss Rate", 25.00)
    chkInt(opt1, "Evictions", 0)

    print("Running Test 2: Thrashing Behavior")
    opt2 = solve(
        simull,
        ["R 0x0", "R 0x40", "R 0x0", "R 0x40"],
        ["--size", "64", "--block", "16", "--assoc", "1", "--policy", "lru", "--write", "wb", "--alloc", "wa"]
    )
    chkFloat(opt2, "Hit Rate", 0.00)
    chkFloat(opt2, "Miss Rate", 100.00)
    chkInt(opt2, "Evictions", 3)

    print("Running Test 3: Write Policies")
    opt3 = solve(
        simull,
        ["W 0x0", "W 0x0"],
        ["--size", "16", "--block", "16", "--assoc", "1", "--policy", "fifo", "--write", "wt", "--alloc", "wa"]
    )
    chkFloat(opt3, "Hit Rate", 50.00)
    chkFloat(opt3, "Miss Rate", 50.00)
    chkInt(opt3, "Evictions", 0)

    print("Running Test 4: IGBI Scan Resistance")
    opt4 = solve(
        simull,
        [
            "R 0x400100 0x1000",
            "R 0x400100 0x1000",
            "R 0x400114 0x2000",
            "R 0x400114 0x3000",
            "R 0x400114 0x4000",
            "R 0x400114 0x5000",
            "R 0x400100 0x1000"
        ],
        ["--size", "128", "--block", "64", "--assoc", "2", "--policy", "igbi", "--write", "wb", "--alloc", "wa"]
    )
    assert "Hit Rate:" in opt4
    assert "Miss Rate:" in opt4

    print("\nSuccess: All test cases passed successfully!")

if __name__ == "__main__":
    main()

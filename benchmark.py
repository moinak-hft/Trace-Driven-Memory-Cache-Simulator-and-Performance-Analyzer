import subprocess
import re

def get_value(output, key):
    match = re.search(rf"{key}:\s*([0-9.]+)", output)
    return float(match.group(1)) if match else 0.0

def main():
    policies = ["lru", "fifo", "random", "lfu", "mru", "optimal", "custom"]
    
    print(f"{'POLICY':<10} | {'HIT RATE %':<12} | {'MISS RATE %':<12} | {'EVICTIONS':<12} | {'WRITE BACKS'}")
    print("-" * 68)

    best_policy = ""
    best_hit_rate = -1.0

    for pol in policies:
        cmd = [
            "./cache_simulator", 
            "--size", "32768", 
            "--block", "64", 
            "--assoc", "8", 
            "--policy", pol, 
            "--write", "wb", 
            "--alloc", "wa", 
            "pinatrace.out"
        ]
        
        res = subprocess.run(cmd, capture_output=True, text=True)
        
        if res.returncode != 0:
            print(f"Error running policy {pol}")
            continue
            
        hit_rate = get_value(res.stdout, "Hit Rate")
        miss_rate = get_value(res.stdout, "Miss Rate")
        evictions = int(get_value(res.stdout, "Evictions"))
        write_backs = int(get_value(res.stdout, "Write Backs"))
        
        print(f"{pol.upper():<10} | {hit_rate:<12.2f} | {miss_rate:<12.2f} | {evictions:<12} | {write_backs}")
        
        if hit_rate > best_hit_rate:
            best_hit_rate = hit_rate
            best_policy = pol

    print("-" * 68)
    print(f"\nResult: The best performing algorithm is {best_policy.upper()} with a Hit Rate of {best_hit_rate:.2f}%.")

if __name__ == "__main__":
    main()
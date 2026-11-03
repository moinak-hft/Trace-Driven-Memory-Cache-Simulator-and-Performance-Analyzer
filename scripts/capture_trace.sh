#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
: "${PIN_ROOT:?Set PIN_ROOT to the absolute Linux Intel Pin directory}"
mode="${1:-matrix}"
size="${2:-64}"
output="${3:-$ROOT/traces/real/$mode.trace}"
tool="$PIN_ROOT/source/tools/CacheLab"
mkdir -p -- "$tool" "$(dirname -- "$output")"
cp -- "$ROOT/pin_trace.cpp" "$tool/pin_trace.cpp"
cp -- "$ROOT/pin/Makefile" "$tool/Makefile"
cp -- "$ROOT/pin/makefile.rules" "$tool/makefile.rules"
make -C "$tool" obj-intel64/pin_trace.so TARGET=intel64
"$PIN_ROOT/pin" -t "$tool/obj-intel64/pin_trace.so" -roi 1 -o "$output" -- "$ROOT/target_workload" "$mode" "$size"
test -s "$output"
printf 'Trace written to %s\n' "$output"

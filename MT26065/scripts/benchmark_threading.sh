
#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

BIN="$PROJECT_ROOT/pandemic"
OUT_DIR="$PROJECT_ROOT/results/partB/benchmark"

N="${N:-300}"
DAYS="${DAYS:-60}"
REPEATS="${REPEATS:-3}"
SEED=42

THREAD_COUNTS=(1 2 3 4 7 8 16)
DIVISIONS=(strips blocks interleaved)
MODES=(thread-per-day pool)

mkdir -p "$OUT_DIR"

RAW="$OUT_DIR/benchmark_raw.csv"
SUMMARY="$OUT_DIR/benchmark_summary.csv"

if [[ ! -x "$BIN" ]]; then
    echo "Error: $BIN not found or not executable."
    exit 1
fi

if [[ ! -x /usr/bin/time ]]; then
    echo "Error: /usr/bin/time is required."
    exit 1
fi

echo "======================================"
echo " PART B: PERFORMANCE BENCHMARK"
echo "======================================"
echo "Grid: $N x $N"
echo "Days: $DAYS"
echo "Repeats: $REPEATS"
echo

echo "mode,threads,chunks,division,run,seconds" > "$RAW"

TIMEFILE=$(mktemp)
trap 'rm -f "$TIMEFILE"' EXIT

run_benchmark() {
    local mode="$1"
    local threads="$2"
    local chunks="$3"
    local division="$4"
    local rep="$5"

    local output="$OUT_DIR/output_${mode}_${threads}_${chunks}_${division}_${rep}.csv"

    /usr/bin/time -f '%e' -o "$TIMEFILE" \
        "$BIN" \
        --N "$N" \
        --days "$DAYS" \
        --seed "$SEED" \
        --fixed-days \
        --parallel-mode "$mode" \
        --threads "$threads" \
        --chunks "$chunks" \
        --division "$division" \
        --output "$output" \
        > /dev/null

    local elapsed
    elapsed=$(cat "$TIMEFILE")

    echo "$mode,$threads,$chunks,$division,$rep,$elapsed" >> "$RAW"

    printf "  %-14s threads=%-2s chunks=%-2s division=%-11s run=%s time=%ss\n" \
        "$mode" "$threads" "$chunks" "$division" "$rep" "$elapsed"
}

echo "Running sequential baseline..."
for ((rep=1; rep<=REPEATS; rep++)); do
    run_benchmark sequential 1 1 strips "$rep"
done

for mode in "${MODES[@]}"; do
    echo
    echo "Running mode: $mode"

    for threads in "${THREAD_COUNTS[@]}"; do
        if [[ "$mode" == "thread-per-day" ]]; then
            chunks="$threads"
        else
            chunks=$((2 * threads))
        fi

        for division in "${DIVISIONS[@]}"; do
            for ((rep=1; rep<=REPEATS; rep++)); do
                run_benchmark "$mode" "$threads" "$chunks" "$division" "$rep"
            done
        done
    done
done

echo
echo "Calculating median times, speedup, and efficiency..."

python3 - "$RAW" "$SUMMARY" <<'PY'
import csv
import statistics
import sys
from collections import defaultdict

raw_file, summary_file = sys.argv[1], sys.argv[2]

groups = defaultdict(list)

with open(raw_file, newline="") as f:
    for row in csv.DictReader(f):
        key = (
            row["mode"],
            int(row["threads"]),
            int(row["chunks"]),
            row["division"],
        )
        groups[key].append(float(row["seconds"]))

baseline = statistics.median(groups[("sequential", 1, 1, "strips")])

rows = []

for (mode, threads, chunks, division), times in groups.items():
    median_time = statistics.median(times)
    speedup = baseline / median_time if median_time else 0
    efficiency = speedup / threads

    rows.append({
        "mode": mode,
        "threads": threads,
        "chunks": chunks,
        "division": division,
        "median_seconds": round(median_time, 4),
        "speedup": round(speedup, 4),
        "efficiency": round(efficiency, 4),
    })

rows.sort(key=lambda r: (
    r["mode"], r["threads"], r["division"]
))

fields = [
    "mode", "threads", "chunks", "division",
    "median_seconds", "speedup", "efficiency"
]

with open(summary_file, "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    writer.writerows(rows)

print(f"\nSequential baseline: {baseline:.4f} seconds")
print("\n{:<15} {:>7} {:<12} {:>10} {:>10} {:>12}".format(
    "Mode", "Threads", "Division", "Time(s)", "Speedup", "Efficiency"
))
print("-" * 75)

for r in rows:
    print("{:<15} {:>7} {:<12} {:>10.4f} {:>10.3f} {:>11.1%}".format(
        r["mode"], r["threads"], r["division"],
        r["median_seconds"], r["speedup"], r["efficiency"]
    ))

print(f"\nRaw results: {raw_file}")
print(f"Summary:     {summary_file}")
PY

echo
echo "======================================"
echo " BENCHMARK COMPLETED"
echo "======================================"

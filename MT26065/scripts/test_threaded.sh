
#!/bin/bash

set -euo pipefail

# ======================================
# PROJECT DIRECTORY
# ======================================

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

PROGRAM="$PROJECT_ROOT/pandemic"

if [ ! -x "$PROGRAM" ]; then
    echo "ERROR: pandemic executable not found or not executable."
    echo "Expected location: $PROGRAM"
    exit 1
fi

# ======================================
# TEST PARAMETERS
# ======================================

N=${N:-100}
DAYS=${DAYS:-30}
SEED=42

RESULT_DIR="$PROJECT_ROOT/results/partB"
mkdir -p "$RESULT_DIR"

REFERENCE="$RESULT_DIR/sequential.csv"

echo "======================================"
echo " PART B: THREADING CORRECTNESS TEST"
echo "======================================"

echo "Project: $PROJECT_ROOT"
echo "Grid: $N x $N"
echo "Days: $DAYS"
echo "Seed: $SEED"

# ======================================
# STEP 1: SEQUENTIAL REFERENCE
# ======================================

echo ""
echo "Creating sequential reference..."

"$PROGRAM" \
    --N "$N" \
    --days "$DAYS" \
    --seed "$SEED" \
    --fixed-days \
    --parallel-mode sequential \
    --output "$REFERENCE" \
    > /dev/null

if [ ! -s "$REFERENCE" ]; then
    echo "ERROR: Sequential reference was not generated."
    exit 1
fi

echo "Sequential reference created."

# ======================================
# STEP 2: TEST CONFIGURATIONS
# ======================================

THREADS=(1 2 3 4 7 8 16)
DIVISIONS=("strips" "blocks" "interleaved")
MODES=("thread-per-day" "pool")

TOTAL=0
PASSED=0
FAILED=0

for MODE in "${MODES[@]}"; do

    echo ""
    echo "======================================"
    echo " Testing mode: $MODE"
    echo "======================================"

    for T in "${THREADS[@]}"; do

        if [ "$MODE" = "pool" ]; then
            C=$((2 * T))
        else
            C=$T
        fi

        for DIV in "${DIVISIONS[@]}"; do

            TOTAL=$((TOTAL + 1))

            OUTPUT="$RESULT_DIR/${MODE}_${DIV}_T${T}_C${C}.csv"

            echo ""
            echo "Test $TOTAL"
            echo "Mode: $MODE | Threads: $T | Chunks: $C | Division: $DIV"

            "$PROGRAM" \
                --N "$N" \
                --days "$DAYS" \
                --seed "$SEED" \
                --fixed-days \
                --threads "$T" \
                --chunks "$C" \
                --division "$DIV" \
                --parallel-mode "$MODE" \
                --output "$OUTPUT" \
                > /dev/null

            if [ ! -s "$OUTPUT" ]; then
                echo "FAIL: Output file missing or empty."
                FAILED=$((FAILED + 1))
                exit 1
            fi

            if cmp -s "$REFERENCE" "$OUTPUT"; then
                echo "PASS"
                PASSED=$((PASSED + 1))
            else
                echo "FAIL: Output differs from sequential reference."
                FAILED=$((FAILED + 1))

                echo ""
                echo "Difference:"
                diff -u "$REFERENCE" "$OUTPUT" || true

                echo ""
                echo "Stopping tests."
                exit 1
            fi

        done
    done
done

# ======================================
# FINAL RESULT
# ======================================

echo ""
echo "======================================"
echo " TEST SUMMARY"
echo "======================================"

echo "Total tests: $TOTAL"
echo "Passed:      $PASSED"
echo "Failed:      $FAILED"

if [ "$PASSED" -eq "$TOTAL" ]; then
    echo ""
    echo "ALL TESTS PASSED!"
else
    echo ""
    echo "SOME TESTS FAILED!"
    exit 1
fi

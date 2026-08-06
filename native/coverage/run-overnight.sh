#!/bin/bash
# Overnight lockstep-validation coverage suite. Sequential runs; each writes
# a full log and a unique-instruction coverage list to native/coverage/out/.
set -e
cd "$(dirname "$0")/../.."
OUT=native/coverage/out
mkdir -p "$OUT"

run() {
    name=$1; frames=$2; script=$3
    echo "=== $name ($frames frames) $(date '+%H:%M:%S') ==="
    args=(./native/harness/harness "dist/nhl94-build.bin" --frames "$frames"
          --validate --covout "$OUT/cov-$name.txt" --out "$OUT")
    [ -n "$script" ] && args+=(--script "$script")
    docker run --rm -v "$PWD":/work -w /work nhl94-dev "${args[@]}" \
        > "$OUT/$name.log" 2>&1 || echo "$name FAILED"
    tail -6 "$OUT/$name.log"
}

python3 native/coverage/gen-random-input.py 1 150000 > "$OUT/rand1.txt"
python3 native/coverage/gen-random-input.py 2 150000 > "$OUT/rand2.txt"

run attract 150000 ""
run rand1 150000 "$OUT/rand1.txt"
run rand2 150000 "$OUT/rand2.txt"

echo "=== merged unique coverage ==="
cat "$OUT"/cov-*.txt | sort -u | wc -l
grep -h "divergences=" "$OUT"/*.log
echo "=== overnight done $(date '+%H:%M:%S') ==="

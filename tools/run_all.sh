#!/usr/bin/env bash
# Run dp on every inputXXX case in a directory, the way the grader does,
# check each result and tabulate the numbers.
#
# Usage: tools/run_all.sh CASES_DIR [OUT_DIR] [-- extra dp flags]
#   CASES_DIR  directory with inputXXX.{aux,nodes,nets,pl,scl,wts}
#   OUT_DIR    work/result directory (default: ./results)
# Produces OUT_DIR/results.csv and OUT_DIR/results.md with, per case:
# cells, wall time, peak memory, D_total, D_max, HPWL (legal), HPWL (SA),
# HPWL improvement, and the checker verdict. If CASES_DIR also holds
# inputXXX.ref.pl (a known legal placement, e.g. Dragon's, written by
# convert_ibm.py --gp), its D_total / D_max measured from the input placement
# and its HPWL are added as reference columns. dp never sees that file.
set -u
if [ $# -lt 1 ]; then
    sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CASES=$(cd "$1" && pwd)
OUT=${2:-results}
shift $(( $# >= 2 ? 2 : 1 ))
[ "${1:-}" = "--" ] && shift
EXTRA=("$@")
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

echo "building dp ..."
cmake -S "$ROOT/M11515075" -B "$OUT/build" > "$OUT/build.log" 2>&1 &&
    cmake --build "$OUT/build" >> "$OUT/build.log" 2>&1 || { echo "build failed, see $OUT/build.log"; exit 1; }

CSV="$OUT/results.csv"
echo "case,cells,seconds,peak_mb,d_total,d_max,hpwl_legal,hpwl_sa,improve_pct,checker,ref_d_total,ref_d_max,ref_hpwl" > "$CSV"
for aux in "$CASES"/input[0-9][0-9][0-9].aux; do
    [ -e "$aux" ] || { echo "no inputXXX.aux in $CASES"; exit 1; }
    name=$(basename "$aux" .aux)
    num=${name#input}
    work="$OUT/work"
    rm -rf "$work" && mkdir -p "$work"
    for ext in aux nodes nets pl scl wts; do
        [ -e "$CASES/$name.$ext" ] && cp "$CASES/$name.$ext" "$work/"
    done
    cp "$OUT/build/dp" "$work/dp"
    # Wall time and peak RSS of the dp process.
    stats=$(cd "$work" && python3 - "output$num" "${EXTRA[@]}" <<'EOF'
import resource, subprocess, sys, time
out = sys.argv[1]
t = time.monotonic()
with open(out + ".stdout", "w") as so, open(out + ".stderr", "w") as se:
    rc = subprocess.call(["./dp", "input" + out[6:], out] + sys.argv[2:], stdout=so, stderr=se)
dt = time.monotonic() - t
mb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1024.0
print("%d %.2f %.1f" % (rc, dt, mb))
EOF
)
    read -r rc secs mb <<< "$stats"
    so="$work/output$num.stdout"
    val() { sed -n "$1p" "$so" | awk '{print $NF}'; }
    cells=$(grep -m1 -i '^ *NumNodes' "$work/$name.nodes" | awk '{print $NF}')
    terms=$(grep -m1 -i '^ *NumTerminals' "$work/$name.nodes" | awk '{print $NF}')
    cells=$(( cells - terms ))
    if [ "$rc" != 0 ]; then
        verdict="dp exit $rc"
        dt=""; dm=""; hl=""; hs=""; imp=""
    else
        verdict=$(python3 "$ROOT/tools/checker.py" "$work/$name" "$work/output$num" --stdout "$so" | tail -1 | sed 's/RESULT: //')
        dt=$(val 1); dm=$(val 2); hl=$(val 3); hs=$(val 4)
        imp=$(python3 -c "print('%.2f' % (100.0 * (1 - $hs / $hl)))")
    fi
    ref=",,"
    [ -e "$CASES/$name.ref.pl" ] && ref=$(python3 "$ROOT/tools/ref_metrics.py" "$work/$name" "$CASES/$name.ref.pl")
    echo "$name,$cells,$secs,$mb,$dt,$dm,$hl,$hs,$imp,$verdict,$ref" >> "$CSV"
    printf '%-9s %7s cells %7ss %7s MB  Dtot %-14s Dmax %-10s HPWL %s -> %s (%s%%)  %s  ref %s\n' \
        "$name" "$cells" "$secs" "$mb" "$dt" "$dm" "$hl" "$hs" "$imp" "$verdict" "$ref"
    mkdir -p "$OUT/outputs"
    cp "$work/output$num"_legal.pl "$work/output$num".pl "$work/output$num".stdout "$work/output$num".stderr "$OUT/outputs/" 2>/dev/null
done
python3 - "$CSV" "$OUT/results.md" <<'EOF'
import csv, sys
rows = list(csv.reader(open(sys.argv[1])))
hdr = ["case", "cells", "time (s)", "peak MB", "D_total", "D_max", "HPWL legal", "HPWL SA", "HPWL -%", "checker",
       "ref D_total", "ref D_max", "ref HPWL"]
with open(sys.argv[2], "w") as f:
    f.write("| " + " | ".join(hdr) + " |\n|" + "---|" * len(hdr) + "\n")
    for r in rows[1:]:
        f.write("| " + " | ".join(r) + " |\n")
EOF
echo "wrote $CSV and $OUT/results.md"

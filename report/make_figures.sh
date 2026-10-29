#!/usr/bin/env bash
# Regenerate the report's run data and figures.
# Usage: report/make_figures.sh CASES_DIR
#   CASES_DIR holds input001..012 built by tools/convert_ibm.py (see outline.md).
# Writes report/data/*.csv and report/figures/*.png. Takes about 15 minutes.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CASES=$(cd "$1" && pwd)
WORK=$(mktemp -d)
DP="$WORK/build/dp"
cmake -S "$ROOT/M11515075" -B "$WORK/build" > /dev/null && cmake --build "$WORK/build" > /dev/null

# T1/T3: full 12-case run (serial, like the grader).
"$ROOT/tools/run_all.sh" "$CASES" "$WORK/final" > /dev/null
cp "$WORK/final/results.csv" "$ROOT/report/data/run_final.csv"

# T2: Stage 1 ablation with SA off.
cd "$WORK"
echo "case,config,d_total,d_max,stage1_s" > "$ROOT/report/data/stage1_ablation.csv"
for aux in "$CASES"/input[0-9][0-9][0-9].aux; do
    n=$(basename "$aux" .aux)
    for cfg in "abacus:--no-l1 --no-postpass" "abacus+l1:--no-postpass" "abacus+post:--no-l1" "full:"; do
        # shellcheck disable=SC2086
        "$DP" "$CASES/$n" ab ${cfg#*:} --no-sa -v > ab.out 2> ab.err
        echo "$n,${cfg%%:*},$(sed -n 1p ab.out | awk '{print $NF}'),$(sed -n 2p ab.out | awk '{print $NF}'),$(grep 'stage 1 done' ab.err | grep -oE '[0-9.]+s' | tr -d s)" \
            >> "$ROOT/report/data/stage1_ablation.csv"
    done
done

# Figures: SA curves (mild and harsh ibm12-cu85) and a placement zoom.
for n in 005 011; do
    "$DP" "$CASES/input$n" out$n --sa-log "$ROOT/report/data/sa_log_input$n.csv" > /dev/null
done
python3 "$ROOT/tools/plot_sa.py" "$ROOT/report/data/sa_log_input005.csv" "$ROOT/report/data/sa_log_input011.csv" \
    -o "$ROOT/report/figures/fig_sa_curves.png" \
    --title "input005: ibm12-cu85, mild GP" "input011: ibm12-cu85, harsh GP"
python3 "$ROOT/tools/plot_placement.py" "$CASES/input011" out011 \
    -o "$ROOT/report/figures/fig_placement_zoom.png" --zoom 0 0 6600 4032
python3 "$ROOT/report/make_tables.py"
echo "done; work files in $WORK"

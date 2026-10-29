# Report outline (≤ 3 pages, PDF, M11515075.pdf)

Material in this directory:

| item | file | use in report |
|---|---|---|
| Fig. 1 SA current / best-so-far HPWL and temperature, mild and harsh large case | `figures/fig_sa_curves.png` | §3, full width, ~6 cm high |
| Fig. 2 GP / Stage 1 / Stage 2 zoom (input011, 8 rows × 100 sites) | `figures/fig_placement_zoom.png` | §4, full width, ~4 cm high |
| Table A final results | `tables.md` T1 | §4 (condense: drop "cells", "checker", "Dragon HPWL"; keep 12 rows) |
| Table B Stage 1 ablation | `tables.md` T2 (mean row + 2–3 cases) | §2 |
| Table C what each change was worth | `ablation.md` "Kept" + "Tried and dropped" | §4, merged into one compact table |
| supporting detail (not printed) | `tables.md` T3–T5, `data/*.csv` | numbers quoted in the text |

Page budget: p.1 = §1 + §2, p.2 = §3 + Fig. 1, p.3 = §4 (Tables A, C, Fig. 2) + §5.

---

## 1. Problem and overview (≈ 0.2 page)

* Input: Bookshelf GP, single-height cells, fixed terminals. Output: legal
  placement (row, site, subrow, no overlap) minimizing D_total and D_max
  (Stage 1), then SA on HPWL from cell centers with legality kept (Stage 2).
* Pipeline in one sentence + constraints that shaped it: 60 s per case
  including I/O; 45 s SA cap from program start; Stage 1 ≤ 10 s safety
  deadline (real cases take ≤ 1.04 s).
* Correctness guards: every result is fully legality-checked inside `dp`
  before writing, and independently by `tools/checker.py`. That check caught
  a real bug during development (a same-segment swap that left the segment's
  cell list stale).

## 2. Stage 1 — legalization (≈ 0.8 page)

* **Row model**: subrows cut by non-NI terminals into free segments; all
  positions are integer site indices → site alignment by construction.
* **Abacus** (Spindler ISPD'08), cells by global x; for each cell the rows
  are visited by increasing |Δy| and segments by horizontal distance; the
  search stops when the lower bound ≥ best cost, so it is exact for Abacus'
  cost without fixing a row window. Cluster snapping by rounding is monotone →
  no overlaps.
* **Guaranteed legal output**: fragmentation fallback (failed cells first,
  then widest-first); infeasible inputs (capacity < demand) exit with an
  error instead of an illegal placement. Tested with toy3 / toy4.
* **L1 reposition**: Abacus minimizes squared displacement; with the order
  fixed, the exact L1 optimum is isotonic regression → PAV with block medians
  (two-heap blocks, small-into-large merge). Applied per segment only if its
  max displacement does not grow (keeps D_max safe).
* **Post-pass rounds**: (a) reduce max — repeatedly move the worst cell into
  the best free gap or swap it with an equal-width cell when the pair's max
  drops; (b) reduce total — every cell, worst first, gap move or swap that
  lowers the pair's sum without exceeding the current max; (c) L1 again.
  Stop below 0.1 % D_total / 1 % D_max gain. Search capped (40 rows, 64
  gaps per direction) so pathological GPs cannot stall it (all-at-origin
  ibm10: 15.0 s → 5.8 s).
* **Table B** (mean of 12): L1 alone −0.4 % D_total; post-pass −6.2 % / −12.4 %;
  full −9.4 % D_total / −21.2 % D_max, ≤ 1.04 s.
* Trade-off sentence: D_max and D_total compete — the max-first loop is
  allowed to raise the pair's sum by at most the max reduction; total-phase
  swaps are capped at the current max.

## 3. Stage 2 — simulated annealing (≈ 0.9 page + Fig. 1)

* **State and legality**: per-segment occupancy (site → cell); every move is
  checked locally, so the placement is legal at every step (no repair).
* **Moves**: 40 % optimal region (median of net bounding boxes without the
  cell) → free sites / equal-width swap / generalized swap / nearest free run;
  20 % random window; 20 % adjacent swap (keeps the pair's span); 20 % in-gap
  shift. Validity per move type (T5): 56 / 68 / 99.5 / 17.5 %.
* **Incremental HPWL**: per-net box with edge pin counts; recompute only when
  an edge loses its last pin (or for nets < 12 pins).
* **Schedule** (justify each choice with T4):
  - T0 from sampled uphill Δ, targeting 1 % acceptance (start is already
    good; 20 % cost 1.5 %);
  - VPR-style α by acceptance, 0.95 in the low-acceptance band (vs 0.8:
    −0.45 %); compressed when needed so T reaches 1e-4·T0 by the 45 s cap;
  - range window starts at 10 % of the chip (smaller start was 0.4–1.3 %
    worse) and adapts to 44 % acceptance;
  - 20 moves per cell per step; on large designs reduced so ~150 steps fit.
* **Stopping**: 45 s cap, or acceptance < 5 % and best gain < 0.01 % over 5
  steps and ≥ 10 moves per cell. Zero-ΔHPWL moves are accepted (plateau
  exploration) but not counted in the acceptance rate — otherwise it stays
  ~17 % at T≈0 and SA never stops.
* **Best-so-far**: undo journal with per-cell dirty flags (journal ≤ N); the
  best HPWL in the CSV and on stdout always belongs to a stored placement.
* **Fig. 1 discussion**: mild GP — short exploration hump (current rises,
  best flat for ~10 % of the run), then the main descent; harsh GP —
  descends immediately (−25.9 %). Both large cases end at the time cap while
  still improving slowly → SA is time-limited at this size; ibm01 converges
  and stops at 20–24 s.

## 4. Experiments (≈ 0.9 page + Tables A, C + Fig. 2)

* **Test cases**: ibm-place2 ibm01/10/12, all six utilization variants
  (0.85–0.95), 12–69 k cells. The benchmark `.pl` puts every cell at (0,0)
  and pads are on no net, so GPs were made from Dragon's legal placements +
  Gaussian noise: mild σ = 1 row / 10 sites, harsh 3 rows / 30 sites (seed 1).
  Dragon's placement is therefore a known legal solution for the same GP →
  reference column for D_total / D_max (and HPWL).
* **Table A**: Stage 1 D_total is 48–60 % of Dragon's on mild cases and
  18–24 % on harsh cases; D_max is lower than Dragon's on 10 of 12 cases —
  the exceptions are mild ibm10-cu95 (input004: 3939 vs 3920, +0.5 %) and
  mild ibm12-cu90 (input006: 4101 vs 3920, +4.6 %) — both the
  higher-utilization variant of their benchmark. SA reduces HPWL by 11.5–13.6 %
  (mild, large) to 25–51 % (harsh / small) and ends 2.4–18.5 % below Dragon's
  HPWL on every case. Runtime ≤ 45.1 s, peak memory ≤ 49 MB.
  Caveat: Dragon optimized HPWL with pin offsets; we use cell centers.
* **Table C**: kept changes with measured effect; tried-and-dropped. Phase-5
  tuning totals −4.3 % mean HPWL on 12 cases, every case better.
* **Fig. 2**: zoom on input011 — overlapping GP, Abacus rows with small
  displacement, SA regrouping cells into connected clusters (more whitespace
  between groups).
* **Robustness**: all-at-origin ibm cases (degenerate GP) finish in ≤ 45.1 s;
  210 k-cell synthetic stress case (3× ibm12) in 45.4 s; 99.5 % utilization
  synthetic case legalizes.

## 5. Limitations and possible extensions (≈ 0.2 page)

* Time-driven schedule → results vary slightly between machines (`--iters`
  gives bit-for-bit reproducibility); run-to-run spread ≈ 0.4–1 %.
* Large designs are time-limited: multi-threaded SA over disjoint regions or
  a final row-local permutation pass would be the next steps.
* Pin offsets are ignored by specification; HPWL against Dragon is therefore
  only indicative.

---

## Reproducing the material

* Cases: `tools/convert_ibm.py raw/ibmNN ibmNN cuXX cases/inputXXX --gp
  pl-files/ibmNN-cuXX-dr.pl --sigma-rows {1|3} --sigma-sites {10|30} --seed 1`
  (order: ibm01 cu85, cu88, ibm10 cu90, cu95, ibm12 cu85, cu90; mild then
  harsh).
* `report/make_figures.sh CASES_DIR` regenerates `data/run_final.csv`,
  `data/stage1_ablation.csv`, the SA logs and both figures, then
  `make_tables.py`. `data/run_phase4_baseline.csv`,
  `data/run_phase5_schedule.csv`, `data/sa_sweeps.csv` and
  `data/move_validity.csv` record runs of earlier code versions and the
  phase-5 parameter sweeps; they are kept as measured, not regenerated.
* Time-driven runs differ slightly from run to run, so regenerated numbers
  will move by up to about 1 %.

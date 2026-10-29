# dp — Standard Cell Detailed Placer (CAD Project 3)

Two-stage detailed placer for GSRC Bookshelf designs:

1. **Stage 1 – Legalization**: legal placement minimizing total and maximum
   displacement from the global placement (see below).
2. **Stage 2 – Simulated annealing**: minimizes total HPWL while keeping the
   placement legal at every step (see below).

Only the C++17 standard library is used.

## Build

```sh
cd M11515075
cmake -B build          # defaults to Release (-O2) when no build type is given
cmake --build build     # produces build/dp
```

## Run

```sh
cd build
./dp input001 output001          # reads input001.aux/.nodes/.nets/.pl/.scl/.wts
```

`argv[1]` is the input base name and `argv[2]` the output base name (no
extensions). With fewer than two arguments `dp` prints an error and exits
with status 1. Optional flags after the two names:

| flag | meaning |
| --- | --- |
| `-v`, `--verbose` | progress, timing and per-step displacement on stderr |
| `--no-l1` | skip the per-segment L1 reposition (for experiments) |
| `--no-postpass` | skip the displacement post-pass (for experiments) |
| `--no-sa` | skip Stage 2; `<out>.pl` is the Stage 1 result |
| `--seed N` | SA random seed (default 1) |
| `--iters N` | fixed SA move budget: the schedule ignores the clock, so a run is bit-for-bit reproducible (the 45 s safety cap still applies) |
| `--sa-cap S` | SA stops at S seconds after program start (default 45) |
| `--sa-log FILE` | write SA progress as CSV (or set env `DP_SA_LOG=FILE`); off by default |
| `--log-every N` | one CSV row every N attempted moves (default: 10 rows per temperature step) |
| `--t0-accept P`, `--moves-per-cell M`, `--win0 R`, `--low-alpha A`, `--target-steps N`, `--mix O,R,A` | tuning overrides of the SA constants in `src/Annealer.h`, for experiments only; the defaults are the tuned values |

The CSV columns are `iter,current_hpwl,best_hpwl,temperature,accept_rate`.
Logging never changes stdout or the output files.

## Output

All files are written to the current working directory:

| file | content |
| --- | --- |
| `<out>_legal.pl` | Stage 1 legal placement |
| `<out>.pl` | Stage 2 best placement found by SA |
| `<out>.aux` | the input `.aux` with every file name renamed to `<out>.*` |
| `<out>.nodes`, `.nets`, `.scl`, `.wts` | byte-for-byte copies of the inputs |

Fixed objects are written with their original coordinate text, and every
object keeps its orientation and `/FIXED` tag. stdout contains exactly four
lines:

```
Total displacement (legal): <float>
Maximum displacement (legal): <float>
Total HPWL (legal): <num>
Total HPWL (SA): <num>
```

## Stage 1 algorithm

1. **Row model** (`Layout.cpp`): every subrow is cut by the fixed, non-NI
   objects overlapping its row into free *segments*; all positions are
   integer site indices of a segment, so site alignment holds by construction.
2. **Abacus** (`Legalizer.cpp`): cells sorted by global x; each cell tries
   the rows nearest its global y and, within a row, the segments nearest its
   global x, using Abacus' cluster collapse as a trial. The search is pruned
   once the vertical distance (or a segment's horizontal lower bound) exceeds
   the best cost so far, so it is exact with respect to the Abacus cost.
   Clusters are snapped to sites by rounding, which is monotone, so no overlap
   is introduced.
   *Fallback:* when fragmented capacity leaves cells unplaced, legalization
   restarts with those cells first (widest first), then fully widest-first.
   If cells still do not fit, the input has too little row capacity and `dp`
   exits with an error.
3. **L1 reposition**: with the cell order in a segment fixed, the positions
   minimizing Σ|x − x_gp| are found exactly by pool-adjacent-violators with
   block medians. A segment adopts them only if neither its maximum nor its
   total displacement grows.
4. **Post-pass rounds** (up to 20, stop when a round gains < 0.1 % of D_total
   and < 1 % of D_max, or at 10 s after program start):
   * *reduce max*: repeatedly take the currently worst cell and move it to the
     free gap with the smallest displacement, or swap it with an equal-width
     cell so that the pair's larger displacement drops; stop when the worst
     cell cannot improve;
   * *reduce total*: every cell, worst first, moves to a strictly better free
     gap or swaps with an equal-width cell to lower the pair's total without
     exceeding the current maximum;
   * the L1 reposition again.

   Each post-pass search visits at most 40 rows and 64 gaps or swap
   candidates per direction and segment, so pathological inputs (e.g. every
   cell at one point) cannot stall it.
5. A full legality check runs on the result before anything is written.

## Stage 2 algorithm (`Annealer.cpp`)

* **State**: one occupancy array per free segment (site → cell). Every move
  is checked locally against it, so the placement is legal at all times.
* **Moves** (cell picked uniformly at random):
  * 40 % *optimal region*: target the median of the bounding boxes of the
    cell's nets (computed without the cell), limited by the range window;
    the cell moves into free sites there, swaps with an equal-width
    occupant, swaps with a different-width occupant when each fits into the
    other's slot (its sites plus the free sites around it), or moves to the
    nearest free run of sites in that segment;
  * 20 % *random window*: the same, at a random point within the window;
  * 20 % *adjacent swap*: swap with the right neighbour in the row, keeping
    the pair's span;
  * 20 % *shift*: move within the free gap around the cell, towards its
    optimal x half of the time.
* **Incremental HPWL**: every net caches its bounding box and the number of
  pins on each edge; a move only touches the nets of the moved cells, and a
  box is recomputed from scratch only when an edge loses its last pin (or
  for nets with fewer than 12 pins, where recomputing is cheaper).
* **Schedule**: T0 is set so that about 1 % of sampled uphill moves would be
  accepted (the start is already a good legal placement). Cooling follows a
  VPR-style rule (α = 0.5 / 0.9 / 0.95 / 0.95 for acceptance > 96 % / > 80 % /
  > 15 % / below). It is sped up when needed so that T reaches 1e-4·T0 by the
  45 s cap. The range window starts at 10 % of the chip and shrinks or grows
  with the acceptance rate (target 44 %). The moves per step are 20 × #cells,
  reduced on large designs so about 150 steps fit in the time budget. Moves
  with zero HPWL change are accepted but do not count toward the acceptance
  rate.
* **Stopping**: hard cap at 45 s after program start, or early when the
  acceptance rate is below 5 % and best-so-far improved by less than
  0.01 % over the last 5 steps and at least 10 moves per cell. All knobs are
  constants in `Annealer.h`; `--t0-accept`, `--moves-per-cell`, `--win0`,
  `--low-alpha` and `--target-steps` override some of them for experiments.
* **Best-so-far**: an undo journal records each cell changed since the last
  snapshot (once per cell, via a dirty flag). Whenever current HPWL beats
  the best, only those cells are copied into the stored best placement, so
  the reported best always corresponds to a saved placement. The output is
  that placement, with its HPWL recomputed from scratch for stdout. It is
  legality-checked before writing; if it fails, the Stage 1 result is used.
* **Reproducibility**: fixed seed (`--seed`). The default schedule is
  time-driven, so results can differ between machines of different speed.
  `--iters N` removes the clock from the schedule.

## Assumptions

1. All movable cells are one row tall; a legal cell's y equals its row's
   `Coordinate` (lower-left).
2. Subrow extent is `[SubrowOrigin, SubrowOrigin + (NumSites-1)*Sitespacing + Sitewidth]`;
   legal x positions are `SubrowOrigin + k*Sitespacing`.
3. An object is fixed if `.nodes` marks it `terminal`/`terminal_NI` or `.pl`
   tags it `/FIXED`/`/FIXED_NI`. Fixed objects that overlap a row with
   positive area are obstacles that split the subrow; `terminal_NI` /
   `/FIXED_NI` objects are not obstacles.
4. Pin offsets are parsed and ignored; HPWL uses cell centers
   (`x + w/2`, `y + h/2`), all nets have weight 1, `.wts` is ignored.
   A cell listed twice on a net counts once (the bounding box is unchanged).
5. stdout numbers are printed with up to 3 decimals, trailing zeros
   stripped (integers print as integers). Plain `cout` would switch to
   scientific notation for large HPWL values.
6. `<out>.aux` lists `<out>.pl`, the Stage 2 result.
7. Paths in `.aux` are resolved relative to the `.aux` file's directory.
8. A cell occupies `ceil(w / Sitespacing)` sites, and a segment's right end
   is rounded down to a whole site. Both are exact when cell widths are
   multiples of the site spacing and `Sitewidth == Sitespacing`, and
   conservative (never illegal) otherwise.
9. If the rows cannot hold all movable cells, `dp` reports an error and
   exits with status 1 instead of writing an illegal placement.

## Testing

* `tests/toy/` (repository root) holds hand-written cases: several subrows
  per row, a terminal inside a row, a `terminal_NI` overlapping a row, pins
  with and without offsets, tabs and `Key:value` without spaces, all 8
  orientations, a site width of 2 with an odd origin, a fragmentation case
  that needs the Abacus fallback (toy3) and an infeasible case (toy4).
* ibm-place2 cases (ibm01/10/12, every utilization variant) were built with
  `convert_ibm.py` from Dragon placements plus Gaussian noise (mild: σ = 1 row
  / 10 sites, harsh: 3 rows / 30 sites), since the benchmark `.pl` files
  place every cell at the origin.
* Every result is checked by `tools/checker.py`, written independently of
  the C++ code.

## Submission

Only this directory is submitted (zipped as `M11515075.zip`), without
`build/`: `src/`, `CMakeLists.txt`, `README.md`.

## Tools (outside this directory, in the repository's `tools/`)

* `tools/checker.py IN OUT [--stdout FILE]` — independent legality and
  metric checker.
* `tools/gen_synth.py OUT_BASE --cells N` — synthetic test case generator.
* `tools/convert_ibm.py SRC BENCH VARIANT OUT_BASE [--gp PL --sigma-rows R --sigma-sites S]`
  — turn an ibm-place2 variant into an `inputXXX` case; macros become fixed
  terminals, and `--gp` substitutes (perturbed) movable positions from
  another placement of the same netlist.
* `tools/run_all.sh CASES_DIR [OUT_DIR] [-- dp flags]` — builds dp, runs every
  `inputXXX` the way the grader does (inputs copied next to `dp`, run from
  that directory), checks each result and writes `results.csv` / `results.md`
  with time, peak memory, D_total, D_max and HPWL before and after SA. When
  the case directory also holds `inputXXX.ref.pl` (written by
  `convert_ibm.py --gp`), reference columns give that known legal
  placement's D_total, D_max and HPWL.
* `tools/ref_metrics.py IN REF_PL` — displacement and HPWL of a reference
  placement against a case's input placement (used by `run_all.sh`).
* `tools/plot_sa.py run.csv -o sa.png` — current and best-so-far HPWL vs
  attempted iterations, with temperature in a panel below (needs matplotlib).
* `tools/plot_placement.py IN OUT -o place.png [--zoom X0 Y0 X1 Y1] [--color-disp]`
  — global / legal / SA placements side by side.

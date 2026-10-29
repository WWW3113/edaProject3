# dp — Standard Cell Detailed Placer (CAD Project 3)

Two-stage detailed placer for GSRC Bookshelf designs:

1. **Stage 1 – Legalization**: legal placement minimizing total and maximum
   displacement from the global placement (see below).
2. **Stage 2 – Simulated annealing**: minimizes total HPWL while keeping the
   placement legal at every step. *(in progress)*

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
4. **Post-pass rounds** (up to 8, stop when a round gains < 0.3 % of D_total
   and < 1 % of D_max, or at 15 s after program start):
   * *reduce max*: repeatedly take the currently worst cell and move it to the
     free gap with the smallest displacement, or swap it with an equal-width
     cell so that the pair's larger displacement drops; stop when the worst
     cell cannot improve;
   * *reduce total*: every cell, worst first, moves to a strictly better free
     gap or swaps with an equal-width cell to lower the pair's total without
     exceeding the current maximum;
   * the L1 reposition again.
5. A full legality check runs on the result before anything is written.

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

## Tools (outside this directory, in the repository's `tools/`)

* `tools/checker.py IN OUT [--stdout FILE]` — independent legality and
  metric checker.
* `tools/gen_synth.py OUT_BASE --cells N` — synthetic test case generator.

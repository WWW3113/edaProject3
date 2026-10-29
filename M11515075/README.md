# dp — Standard Cell Detailed Placer (CAD Project 3)

Two-stage detailed placer for GSRC Bookshelf designs:

1. **Stage 1 – Legalization**: legal placement minimizing total and maximum
   displacement from the global placement. *(in progress)*
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
| `-v`, `--verbose` | progress and timing on stderr |

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

## Tools (outside this directory, in the repository's `tools/`)

* `tools/checker.py IN OUT [--stdout FILE]` — independent legality and
  metric checker.
* `tools/gen_synth.py OUT_BASE --cells N` — synthetic test case generator.

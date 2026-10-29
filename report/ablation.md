# Ablation: what changed and what each change was worth

Condensed for the report. Detailed numbers: `tables.md` (T2–T5), raw data in
`data/`. Stage 1 numbers are means over the 12 ibm cases. SA numbers come from
three concurrent runs on input001 / input005 / input011 unless marked "12
cases". Run-to-run spread of the time-driven SA is about 0.4–1 %, so smaller
differences are reported as "no effect".

## Kept

| stage | change | measured effect | evidence |
|---|---|---|---|
| 1 | Abacus with pruned multi-row search (baseline) | legal on all cases; 0.03–1.0 s | T2 |
| 1 | + L1 reposition (PAV with block medians, only if a segment's max displacement does not grow) | D_total −0.4 %, D_max ±0 when used alone | T2 |
| 1 | + post-pass rounds (gap moves, equal-width swaps), no L1 | D_total −6.2 %, D_max −12.4 % | T2 |
| 1 | full Stage 1 (post-pass with L1 inside every round) | D_total −9.4 %, D_max −21.2 % vs Abacus; ≤ 1.04 s | T2 |
| 1 | post-pass search capped (40 rows, 64 gaps/candidates per direction) | ≤ 0.04 % change on ibm cases; all-at-origin stress case: Stage 1 15.0 s → 5.8 s | phase 5 runs |
| 2 | zero-ΔHPWL moves no longer count toward the acceptance rate | synth_s: stopped at the 45 s cap → converged in 1.6 s (HPWL +0.6 %) | phase 4 runs |
| 2 | nearest free run found by one linear pass (was 35 % of SA time) | input005 −0.7 %, input011 −0.6 % | phase 5 runs |
| 2 | T0 targets 1 % uphill acceptance instead of 20 % | −1.5 % | T4 `t0_accept` |
| 2 | cooling α 0.95 (was 0.8) below 15 % acceptance | −0.45 % | T4 `window_alpha` |
| 2 | 20 moves per cell per step (was 10) | −0.8 % (mostly input001: −1.9 %) | T4 `alpha_mpc` vs `window_alpha` |
| 2 | early-stop patience also needs 10 moves per cell | prevents premature stops (300-step schedule had stopped input005 at 31.8 s) | T4 `steps` |
| 2 | generalized swap (different widths, each fits the other's slot) | −1.3 % (3 cases); −1.4 % mean on 12 cases | T4 `genswap`, T3 |
| 2 | all phase-5 SA changes together | −4.3 % mean, every one of 12 cases better (−2.7 % … −6.0 %) | T3 (12 cases) |

## Tried and dropped

| change | result | why dropped |
|---|---|---|
| initial range window 10 rows / 3 rows (instead of 10 % of chip) | +0.4 … +1.3 % worse | early chip-wide moves matter |
| T0 at 0.2 % uphill acceptance | +0.2 % (no gain) | no benefit over 1 % |
| α 0.9 or 0.98 below 15 % acceptance (same 10 moves/cell) | +0.3 % / +0.05 % vs 0.95 | within noise; 0.95 kept |
| 300 temperature steps per budget | +0.9 … +1.0 % worse | steps too short to equilibrate |
| 75 temperature steps | −0.26 % (within noise) | kept 150 |
| other move mixes (60/10/15/15, 30/30/20/20, 50/20/10/20, 40/20/35/5) | −0.22 … +0.13 % | within noise; mix is not sensitive |
| displacement-coloured placement figure | colour scale dominated by SA moves | plain figure is clearer |

## Behaviour notes for the discussion

* Valid proposals per move type (T5): optimal-region 52 % → 56 % with the
  generalized swap, random window 68 %, adjacent swap 99.5 %, in-gap shift
  17.5 %. Most optimal-region "failures" are cells already inside their
  optimal region, which is correct behaviour rather than wasted work.
* The mild GP starts close to a good placement: SA first climbs (exploration
  hump, about 10 % of the run) before descending; the harsh GP improves from
  the first step (Fig. 1).
* On large cases SA ends at the 45 s cap while still improving slowly; small
  cases (ibm01) converge and stop early at 20–24 s.

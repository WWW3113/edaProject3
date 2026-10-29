#!/usr/bin/env python3
"""Synthetic Bookshelf case generator for testing dp.

Produces a row-based design with an overlapping, clustered "global placement":
  * standard cells of row height and random site-multiple widths
  * rows that are sometimes split into two subrows
  * fixed macros sitting inside the core (row obstacles), some not row aligned
  * terminal_NI objects overlapping rows (must NOT act as obstacles)
  * I/O pads outside the core
  * nets built from spatial neighbourhoods; about half the pin lines omit
    the offset, the rest carry one
Usage:
  gen_synth.py OUT_BASE [--cells N] [--util U] [--macros M] [--seed S]
e.g. gen_synth.py cases/synth_big --cells 210000   (ibm18-like size)
"""
import argparse
import math
import os
import random


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--cells", type=int, default=2000)
    ap.add_argument("--util", type=float, default=0.8, help="cell area / free row area")
    ap.add_argument("--macros", type=int, default=8)
    ap.add_argument("--pads", type=int, default=0, help="default: ~2%% of cells, min 8")
    ap.add_argument("--row-height", type=int, default=16)
    ap.add_argument("--hotspot-frac", type=float, default=0.25)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    H = a.row_height
    N = a.cells

    widths = [rnd.choice((1, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 9, 10, 12, 14, 16, 20, 24))
              for _ in range(N)]
    wsum = sum(widths)
    macro_frac = 0.08 if a.macros else 0.0
    area = wsum * H / (a.util * (1 - macro_frac))
    rows = max(2, int(math.sqrt(area) / H))
    L = int(math.ceil(wsum / (a.util * (1 - macro_frac) * rows)))
    core_h = rows * H

    # Rows: ~10% split into two subrows with a small gap.
    row_spans = []
    for r in range(rows):
        if rnd.random() < 0.1:
            cut = rnd.randint(L // 4, 3 * L // 4)
            gap = max(2, L // 100)
            row_spans.append([(0, cut), (cut + gap, L + gap)])
        else:
            row_spans.append([(0, L)])

    # Macros: fixed blocks inside the core, non-overlapping.
    macros = []
    target = macro_frac * L * core_h
    tries = 0
    while len(macros) < a.macros and tries < 10000:
        tries += 1
        mw = rnd.randint(max(4, L // 30), max(5, L // 10))
        mh = rnd.randint(2, max(3, rows // 8)) * H
        if len(macros) % 3 == 2:
            mh += H // 2  # not a whole number of rows
        mx = rnd.randint(0, max(0, L - mw))
        my = rnd.randint(0, max(0, rows - mh // H - 1)) * H
        if len(macros) % 4 == 3:
            my += H // 4  # not row aligned
        if any(mx < x + w and x < mx + mw and my < y + h and y < my + mh for x, y, w, h in macros):
            continue
        macros.append((mx, my, mw, mh))
        if sum(w * h for _, _, w, h in macros) > target:
            break

    def in_macro(x, y, w):
        for mx, my, mw, mh in macros:
            if x < mx + mw and mx < x + w and y < my + mh and my < y + H:
                return True
        return False

    # Global placement: uniform background plus Gaussian hot spots.
    hot = [(rnd.uniform(0, L), rnd.uniform(0, core_h)) for _ in range(max(1, N // 5000 + 3))]
    sig = 0.03 * max(L, core_h)
    gx, gy = [0.0] * N, [0.0] * N
    for i in range(N):
        for _ in range(8):
            if rnd.random() < a.hotspot_frac:
                cx, cy = rnd.choice(hot)
                x, y = rnd.gauss(cx, sig), rnd.gauss(cy, sig)
            else:
                x, y = rnd.uniform(0, L - widths[i]), rnd.uniform(0, core_h - H)
            x = min(max(x, -0.01 * L), L)
            y = min(max(y, -0.01 * core_h), core_h)
            if not in_macro(x, y, widths[i]):
                break
        gx[i], gy[i] = round(x, 2), round(y, 2)

    P = a.pads or max(8, N // 50)
    pads = []
    for p in range(P):
        side = p % 4
        t = rnd.uniform(0, 1)
        if side == 0:
            pads.append((t * L, -2 * H))
        elif side == 1:
            pads.append((t * L, core_h + H))
        elif side == 2:
            pads.append((-3 * H, t * core_h))
        else:
            pads.append((L + 2 * H, t * core_h))
    nis = [(rnd.uniform(0, L - 10), rnd.uniform(0, core_h - H)) for _ in range(3)]

    cell_names = ["a%d" % i for i in range(N)]
    pad_names = ["p%d" % i for i in range(P)]
    mac_names = ["m%d" % i for i in range(len(macros))]
    ni_names = ["ni%d" % i for i in range(len(nis))]

    # Spatial buckets for neighbourhood nets.
    G = max(1, int(math.sqrt(N / 8)))
    bw, bh = (L + 1) / G, (core_h + 1) / G
    buckets = [[] for _ in range(G * G)]

    def bucket_of(x, y):
        bx = min(G - 1, max(0, int(x / bw)))
        by = min(G - 1, max(0, int(y / bh)))
        return bx, by

    for i in range(N):
        bx, by = bucket_of(gx[i], gy[i])
        buckets[by * G + bx].append(i)

    def degree():
        r = rnd.random()
        if r < 0.55:
            return 2
        if r < 0.75:
            return 3
        if r < 0.97:
            return rnd.randint(4, 10)
        return rnd.randint(11, 60)

    nets = []
    n_nets = int(N * 0.96)
    for _ in range(n_nets):
        k = degree()
        drv = rnd.randrange(N)
        bx, by = bucket_of(gx[drv], gy[drv])
        rad = 1 if k <= 4 else 2
        pins = {("c", drv)}
        guard = 0
        while len(pins) < k and guard < 10 * k:
            guard += 1
            u = rnd.random()
            if u < 0.02:
                pins.add(("p", rnd.randrange(P)))
            elif u < 0.05:
                pins.add(("c", rnd.randrange(N)))
            else:
                nx = min(G - 1, max(0, bx + rnd.randint(-rad, rad)))
                ny = min(G - 1, max(0, by + rnd.randint(-rad, rad)))
                b = buckets[ny * G + nx]
                if b:
                    pins.add(("c", rnd.choice(b)))
        if len(pins) >= 2:
            nets.append(list(pins))
    if macros:  # a few nets touching macros
        for j in range(len(macros)):
            nets.append([("m", j), ("c", rnd.randrange(N)), ("c", rnd.randrange(N))])
    nets.append([("n", 0), ("c", rnd.randrange(N))])

    def pname(kind, i):
        return {"c": cell_names, "p": pad_names, "m": mac_names, "n": ni_names}[kind][i]

    base = a.out
    os.makedirs(os.path.dirname(base) or ".", exist_ok=True)
    bn = os.path.basename(base)
    n_term = P + len(macros) + len(nis)
    with open(base + ".aux", "w") as f:
        f.write("RowBasedPlacement : %s.nodes %s.nets %s.wts %s.pl %s.scl\n" % ((bn,) * 5))
    with open(base + ".nodes", "w") as f:
        f.write("UCLA nodes 1.0\n# synthetic, seed %d\n\nNumNodes : %d\nNumTerminals : %d\n\n"
                % (a.seed, N + n_term, n_term))
        f.write("".join("\t%s\t%d\t%d\n" % (cell_names[i], widths[i], H) for i in range(N)))
        for i, (mx, my, mw, mh) in enumerate(macros):
            f.write("\t%s\t%d\t%d\tterminal\n" % (mac_names[i], mw, mh))
        for i in range(P):
            f.write("\t%s\t1\t1\tterminal\n" % pad_names[i])
        for i in range(len(nis)):
            f.write("\t%s\t10\t%d\tterminal_NI\n" % (ni_names[i], H))
    with open(base + ".nets", "w") as f:
        f.write("UCLA nets 1.0\n\nNumNets : %d\nNumPins : %d\n\n"
                % (len(nets), sum(len(n) for n in nets)))
        out = []
        for j, n in enumerate(nets):
            out.append("NetDegree : %d   n%d\n" % (len(n), j))
            for t, (kind, i) in enumerate(n):
                d = "O" if t == 0 else "I"
                if rnd.random() < 0.5:
                    out.append("\t%s\t%s\n" % (pname(kind, i), d))
                else:
                    out.append("\t%s\t%s : %.1f %.1f\n" % (pname(kind, i), d,
                                                         rnd.uniform(-2, 2), rnd.uniform(-4, 4)))
        f.write("".join(out))
    with open(base + ".wts", "w") as f:
        f.write("UCLA wts 1.0\n\n")
        f.write("".join("\tn%d\t1\n" % j for j in range(len(nets))))
    with open(base + ".pl", "w") as f:
        f.write("UCLA pl 1.0\n\n")
        f.write("".join("%s\t%s\t%s\t: %s\n" % (cell_names[i], repr(gx[i]), repr(gy[i]),
                                                  "N" if rnd.random() < 0.8 else "FS")
                        for i in range(N)))
        for i, (mx, my, mw, mh) in enumerate(macros):
            f.write("%s\t%d\t%d\t: N /FIXED\n" % (mac_names[i], mx, my))
        for i, (px, py) in enumerate(pads):
            f.write("%s\t%d\t%d\t: N /FIXED\n" % (pad_names[i], px, py))
        for i, (x, y) in enumerate(nis):
            f.write("%s\t%d\t%d\t: N /FIXED_NI\n" % (ni_names[i], x, y))
    with open(base + ".scl", "w") as f:
        f.write("UCLA scl 1.0\n\nNumRows : %d\n\n" % rows)
        for r in range(rows):
            f.write("CoreRow Horizontal\n  Coordinate    :   %d\n  Height        :   %d\n"
                    "  Sitewidth     :    1\n  Sitespacing   :    1\n  Siteorient    :    1\n"
                    "  Sitesymmetry  :    1\n" % (r * H, H))
            for s0, s1 in row_spans[r]:
                f.write("  SubrowOrigin  :    %d\tNumSites  :  %d\n" % (s0, s1 - s0))
            f.write("End\n")
    print("%s: %d cells, %d terminals (%d macros), %d nets, %d rows x %d sites, util %.2f"
          % (base, N, n_term, len(macros), len(nets), rows, L, a.util))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Turn an ibm-place2 benchmark variant into a dp test case.

Usage:
  convert_ibm.py SRC_DIR BENCH VARIANT OUT_BASE [--gp PL] [--sigma-rows R]
                 [--sigma-sites S] [--seed N]

SRC_DIR holds BENCH.nodes/.nets/.wts and BENCH-VARIANT.pl/.scl, e.g.
  convert_ibm.py raw/ibm01 ibm01 cu85 cases/input001

* Movable objects taller than the row height (macros) become fixed
  `terminal`s at their .pl position (ibm-place2 has none, but other
  benchmark sets do).
* --gp PL takes movable positions from another placement of the same netlist
  (e.g. a Dragon result) instead of the variant's .pl, and adds Gaussian
  noise: sigma-rows row heights in y and sigma-sites site widths in x, so
  cells overlap and are off-row / off-site like a global placement.
  Terminal positions always come from the variant's .pl.
Writes OUT_BASE.{aux,nodes,nets,wts,pl,scl}.
"""
import argparse
import os
import random
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from checker import read_nodes, read_pl, read_scl  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("bench")
    ap.add_argument("variant")
    ap.add_argument("out")
    ap.add_argument("--gp")
    ap.add_argument("--sigma-rows", type=float, default=0.0)
    ap.add_argument("--sigma-sites", type=float, default=0.0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rnd = random.Random(a.seed)

    src = lambda name: os.path.join(a.src, name)  # noqa: E731
    nodes_path = src(a.bench + ".nodes")
    pl_path = src("%s-%s.pl" % (a.bench, a.variant))
    scl_path = src("%s-%s.scl" % (a.bench, a.variant))
    nodes, order = read_nodes(nodes_path)
    pl = read_pl(pl_path)
    rows = read_scl(scl_path)
    row_h = max(set(r["h"] for r in rows), key=[r["h"] for r in rows].count)
    site = rows[0]["sp"]
    ys = sorted(r["y"] for r in rows)
    xs0 = min(s[0] for r in rows for s in r["subrows"])
    xs1 = max(s[0] + s[1] * r["sp"] for r in rows for s in r["subrows"])

    macros = {n for n in order if not nodes[n]["fixed"] and nodes[n]["h"] > row_h}
    gp = read_pl(a.gp) if a.gp else None
    if gp is not None:
        missing = [n for n in order if not nodes[n]["fixed"] and n not in gp]
        if missing:
            sys.exit("error: %d movable objects (e.g. %s) are missing from %s" % (len(missing), missing[0], a.gp))

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    bn = os.path.basename(a.out)

    # .nodes: copy, marking macros as terminals.
    with open(nodes_path) as f, open(a.out + ".nodes", "w") as g:
        n_term = sum(1 for n in order if nodes[n]["fixed"]) + len(macros)
        for line in f:
            t = line.split()
            if t and t[0] in macros:
                line = line.rstrip("\n") + "\tterminal\n"
            elif t and t[0] == "NumTerminals" and macros:
                line = "NumTerminals : %d\n" % n_term
            g.write(line)
    shutil.copyfile(src(a.bench + ".nets"), a.out + ".nets")
    wts = src(a.bench + ".wts")
    if os.path.exists(wts):
        shutil.copyfile(wts, a.out + ".wts")
    shutil.copyfile(scl_path, a.out + ".scl")

    with open(a.out + ".pl", "w") as g:
        g.write("UCLA pl 1.0\n# %s-%s%s\n\n" % (a.bench, a.variant, (" gp=%s sigma_rows=%g sigma_sites=%g seed=%d"
                                                                     % (os.path.basename(a.gp), a.sigma_rows,
                                                                        a.sigma_sites, a.seed)) if gp else ""))
        for n in order:
            rec = pl[n]
            orient = rec["orient"] or "N"
            if nodes[n]["fixed"] or n in macros:
                tag = " /FIXED" if n in macros else (" " + " ".join(rec["tags"]) if rec["tags"] else "")
                g.write("%s\t%s\t%s\t: %s%s\n" % (n, rec["xs"], rec["ys"], orient, tag))
                continue
            if gp is None:
                g.write("%s\t%s\t%s\t: %s\n" % (n, rec["xs"], rec["ys"], orient))
                continue
            x = gp[n]["x"] + rnd.gauss(0, a.sigma_sites * site)
            y = gp[n]["y"] + rnd.gauss(0, a.sigma_rows * row_h)
            # Keep the perturbed cell inside the core box.
            x = min(max(x, xs0), xs1 - nodes[n]["w"])
            y = min(max(y, ys[0]), ys[-1])
            g.write("%s\t%.2f\t%.2f\t: %s\n" % (n, x, y, orient))
    with open(a.out + ".aux", "w") as g:
        g.write("RowBasedPlacement : %s.nodes %s.nets %s.wts %s.pl %s.scl\n" % ((bn,) * 5))
    print("%s <- %s-%s: %d movable, %d terminals, %d macros fixed%s"
          % (a.out, a.bench, a.variant, sum(1 for n in order if not nodes[n]["fixed"]) - len(macros),
             sum(1 for n in order if nodes[n]["fixed"]), len(macros), ", gp from " + a.gp if gp else ""))


if __name__ == "__main__":
    main()

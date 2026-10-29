#!/usr/bin/env python3
"""Plot global / legal / SA placements side by side.

Usage:
  plot_placement.py IN_BASE OUT_BASE [-o place.png] [--zoom X0 Y0 X1 Y1]
                    [--color-disp]
IN_BASE is the dp input (its .pl is the global placement); OUT_BASE is the
dp output base (<OUT_BASE>_legal.pl and <OUT_BASE>.pl). Movable cells are
blue, fixed objects gray (row obstacles filled, others outlined). With
--color-disp, the legal and SA panels shade cells by displacement from the
global placement (light = small, dark = large).
"""
import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.collections import PolyCollection  # noqa: E402
from matplotlib.colors import LinearSegmentedColormap  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from checker import Checker, read_pl  # noqa: E402

INK, INK2, SURFACE = "#0b0b0b", "#52514e", "#fcfcfb"
CELL = "#2a78d6"
FIXED_FILL, FIXED_EDGE, ROW = "#c3c2b7", "#8a8984", "#efeeea"
DISP = LinearSegmentedColormap.from_list("disp", ["#cde2fb", "#6da7ec", "#256abf", "#0d366b"])


def rects(names, pos, nodes, box):
    out = []
    for n in names:
        p = pos.get(n)
        if p is None:
            continue
        x, y = p["x"], p["y"]
        w, h = nodes[n]["w"], nodes[n]["h"]
        if box and (x + w < box[0] or x > box[2] or y + h < box[1] or y > box[3]):
            continue
        out.append(((x, y), (x + w, y), (x + w, y + h), (x, y + h)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("in_base")
    ap.add_argument("out_base")
    ap.add_argument("-o", "--out", default="place.png")
    ap.add_argument("--zoom", type=float, nargs=4, metavar=("X0", "Y0", "X1", "Y1"))
    ap.add_argument("--color-disp", action="store_true")
    a = ap.parse_args()

    ck = Checker(a.in_base)
    panels = [("global placement", ck.gp)]
    for label, path in (("Stage 1 (legal)", a.out_base + "_legal.pl"), ("Stage 2 (SA)", a.out_base + ".pl")):
        if os.path.exists(path):
            panels.append((label, read_pl(path)))
    fixed = [n for n in ck.order if ck.nodes[n]["fixed"]]
    blocking = [n for n in fixed if not ck.nodes[n]["ni"]]
    nonblocking = [n for n in fixed if ck.nodes[n]["ni"]]
    box = a.zoom

    xs = [seg[0] for r in ck.rows for seg in r["subrows"]]
    xe = [seg[0] + (seg[1] - 1) * r["sp"] + r["sw"] for r in ck.rows for seg in r["subrows"]]
    core = (min(xs), min(r["y"] for r in ck.rows), max(xe), max(r["y"] + r["h"] for r in ck.rows))
    view = box or core

    fig, axes = plt.subplots(1, len(panels), figsize=(4.2 * len(panels), 4.4), facecolor=SURFACE)
    if len(panels) == 1:
        axes = [axes]
    dmax = 0.0
    if a.color_disp and len(panels) > 1:
        for _, pos in panels[1:]:
            for n in ck.movable:
                g, o = ck.gp[n], pos[n]
                dmax = max(dmax, abs(o["x"] - g["x"]) + abs(o["y"] - g["y"]))
    for ax, (label, pos) in zip(axes, panels):
        ax.set_facecolor(SURFACE)
        rows = [((s[0], r["y"]), (s[0] + (s[1] - 1) * r["sp"] + r["sw"], r["y"]),
                 (s[0] + (s[1] - 1) * r["sp"] + r["sw"], r["y"] + r["h"]), (s[0], r["y"] + r["h"]))
                for r in ck.rows for s in r["subrows"]]
        ax.add_collection(PolyCollection(rows, facecolors=ROW, edgecolors="none"))
        ax.add_collection(PolyCollection(rects(blocking, ck.gp, ck.nodes, box),
                                         facecolors=FIXED_FILL, edgecolors=FIXED_EDGE, linewidths=0.4))
        ax.add_collection(PolyCollection(rects(nonblocking, ck.gp, ck.nodes, box),
                                         facecolors="none", edgecolors=FIXED_EDGE, linewidths=0.4,
                                         linestyles="dashed"))
        cells = [n for n in ck.movable if n in pos]
        polys = rects(cells, pos, ck.nodes, box)
        if a.color_disp and pos is not ck.gp and dmax > 0:
            kept = [n for n in cells if rects([n], pos, ck.nodes, box)]
            vals = [(abs(pos[n]["x"] - ck.gp[n]["x"]) + abs(pos[n]["y"] - ck.gp[n]["y"])) / dmax for n in kept]
            pc = PolyCollection(polys, array=vals, cmap=DISP, edgecolors="none")
            pc.set_clim(0, 1)
        else:
            pc = PolyCollection(polys, facecolors=CELL, edgecolors="none", alpha=0.55 if box is None else 0.8)
        if box is not None:
            pc.set_edgecolor(SURFACE)
            pc.set_linewidth(0.3)
        ax.add_collection(pc)
        ax.set_xlim(view[0], view[2])
        ax.set_ylim(view[1], view[3])
        ax.set_aspect("equal")
        ax.set_title(label, color=INK, fontsize=10, loc="left")
        ax.tick_params(colors=INK2, labelsize=7)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
    if a.color_disp and dmax > 0:
        sm = plt.cm.ScalarMappable(cmap=DISP, norm=plt.Normalize(0, dmax))
        fig.colorbar(sm, ax=list(axes), shrink=0.7, label="displacement")
    fig.savefig(a.out, dpi=200, facecolor=SURFACE, bbox_inches="tight")
    print("wrote", a.out)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Plot SA convergence from a dp CSV log (dp ... --sa-log run.csv).

Top panel: current and best-so-far HPWL vs attempted iterations.
Bottom panel (shared x): temperature on a log scale.
Usage: plot_sa.py run.csv [-o sa.png] [--title TEXT]
"""
import argparse
import csv

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

INK, INK2, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
CURRENT, BEST = "#2a78d6", "#eb6834"  # categorical slots 1 and 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("-o", "--out", default="sa.png")
    ap.add_argument("--title", default=None)
    a = ap.parse_args()

    it, cur, best, temp = [], [], [], []
    with open(a.csv) as f:
        for row in csv.DictReader(f):
            it.append(int(row["iter"]))
            cur.append(float(row["current_hpwl"]))
            best.append(float(row["best_hpwl"]))
            temp.append(float(row["temperature"]))

    plt.rcParams.update({"font.size": 9, "axes.edgecolor": INK2, "axes.labelcolor": INK,
                         "xtick.color": INK2, "ytick.color": INK2})
    fig, (ax, axt) = plt.subplots(2, 1, figsize=(6.4, 4.4), sharex=True,
                                  gridspec_kw={"height_ratios": [3, 1]}, facecolor=SURFACE)
    for x in (ax, axt):
        x.set_facecolor(SURFACE)
        x.grid(True, color=GRID, linewidth=0.6)
        for side in ("top", "right"):
            x.spines[side].set_visible(False)

    ax.plot(it, cur, color=CURRENT, linewidth=1.0, label="current HPWL")
    ax.plot(it, best, color=BEST, linewidth=2.0, label="best-so-far HPWL")
    ax.set_ylabel("HPWL")
    ax.legend(frameon=False, loc="upper right")
    ax.ticklabel_format(axis="y", style="sci", scilimits=(0, 0))
    # Direct labels at the end of both lines.
    ax.annotate("start %.4g" % best[0], (it[0], best[0]), textcoords="offset points",
                xytext=(6, -14), color=INK2)
    ax.annotate("best %.4g (%.1f%%)" % (best[-1], 100 * (best[-1] / best[0] - 1)),
                (it[-1], best[-1]), textcoords="offset points", xytext=(-4, 6),
                ha="right", color=INK)

    tpos = [(i, t) for i, t in zip(it, temp) if t > 0]
    if tpos:
        axt.plot([p[0] for p in tpos], [p[1] for p in tpos], color=INK2, linewidth=1.2)
        axt.set_yscale("log")
    axt.set_ylabel("temperature")
    axt.set_xlabel("attempted SA iterations")
    if a.title:
        ax.set_title(a.title, color=INK, loc="left")
    fig.tight_layout()
    fig.savefig(a.out, dpi=200, facecolor=SURFACE)
    print("wrote", a.out)


if __name__ == "__main__":
    main()

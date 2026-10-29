#!/usr/bin/env python3
"""Independent legality / metric checker for the dp detailed placer.

Usage:
    checker.py <input_base> <output_base> [--stdout FILE] [--quiet]

Reads <input_base>.{aux,nodes,nets,pl,scl}, <output_base>_legal.pl and
<output_base>.pl. Checks for both output placements:
  * every object present, fixed objects unchanged (coordinate text),
    orientation and /FIXED tags unchanged
  * movable cells: on a row (y == row y), site aligned, fully inside one
    subrow, no overlap with each other or with row-blocking terminals
Recomputes D_total, D_max (legal) and HPWL (legal, SA). With --stdout,
compares them against dp's 4 stdout lines and checks their format.
Also checks <output_base>.aux and the copied .nodes/.nets/.scl/.wts.

Exit status 0 when everything passes, 1 otherwise. Written separately from
the C++ code on purpose so parser bugs do not cancel out.
"""
import argparse
import os
import re
import sys

EPS = 1e-6


def lines_of(path):
    with open(path, "r") as f:
        for raw in f:
            raw = raw.split("#", 1)[0]
            toks = raw.replace(":", " : ").split()
            if toks:
                yield toks


def is_header(t):
    return t[0].upper() == "UCLA"


def read_aux(base):
    d = os.path.dirname(base)
    files = {}
    path = base + ".aux"
    if os.path.exists(path):
        for t in lines_of(path):
            for tok in t:
                if tok == ":" or "." not in tok:
                    continue
                ext = tok.rsplit(".", 1)[1].lower()
                files[ext] = os.path.join(d, tok)
    for ext in ("nodes", "nets", "pl", "scl", "wts"):
        files.setdefault(ext, base + "." + ext)
    return files


def read_nodes(path):
    nodes = {}
    order = []
    for t in lines_of(path):
        if is_header(t) or t[0].lower() in ("numnodes", "numterminals"):
            continue
        name, w, h = t[0], float(t[1]), float(t[2])
        kinds = [x.lower() for x in t[3:]]
        nodes[name] = {
            "w": w, "h": h,
            "fixed": "terminal" in kinds or "terminal_ni" in kinds,
            "ni": "terminal_ni" in kinds,
        }
        order.append(name)
    return nodes, order


def read_nets(path, nodes):
    nets = []
    cur = None
    for t in lines_of(path):
        if is_header(t) or t[0].lower() in ("numnets", "numpins"):
            continue
        if t[0].lower() == "netdegree":
            cur = []
            nets.append(cur)
            continue
        if t[0] not in nodes:
            raise ValueError("net pin refers to unknown object %s" % t[0])
        cur.append(t[0])
    return nets


def read_pl(path):
    """name -> dict(x, y, xs, ys, orient, tags)"""
    pl = {}
    for t in lines_of(path):
        if is_header(t):
            continue
        rec = {"xs": t[1], "ys": t[2], "x": float(t[1]), "y": float(t[2]),
               "orient": None, "tags": []}
        for tok in t[3:]:
            if tok == ":":
                continue
            if tok.startswith("/"):
                rec["tags"].append(tok)
            elif rec["orient"] is None:
                rec["orient"] = tok
        if t[0] in pl:
            raise ValueError("%s: duplicate entry %s" % (path, t[0]))
        pl[t[0]] = rec
    return pl


def read_scl(path):
    rows = []
    cur = None
    for t in lines_of(path):
        if is_header(t) or t[0].lower() == "numrows":
            continue
        k = t[0].lower()
        if k == "corerow":
            cur = {"y": 0.0, "h": 0.0, "sw": 1.0, "sp": 1.0, "subrows": []}
            rows.append(cur)
            continue
        if k == "end":
            cur = None
            continue
        if cur is None:
            continue
        vals = [x for x in t if x != ":"]
        i = 0
        pending = None
        while i + 1 < len(vals):
            key, v = vals[i].lower(), vals[i + 1]
            if key == "coordinate":
                cur["y"] = float(v)
            elif key == "height":
                cur["h"] = float(v)
            elif key == "sitewidth":
                cur["sw"] = float(v)
            elif key == "sitespacing":
                cur["sp"] = float(v)
            elif key == "subroworigin":
                pending = float(v)
                cur["_pending"] = pending
            elif key == "numsites":
                cur["subrows"].append((cur.pop("_pending"), int(float(v))))
            i += 2
    return rows


class Checker:
    def __init__(self, in_base, quiet=False):
        self.quiet = quiet
        self.errors = []
        self.files = read_aux(in_base)
        self.nodes, self.order = read_nodes(self.files["nodes"])
        self.nets = read_nets(self.files["nets"], self.nodes)
        self.gp = read_pl(self.files["pl"])
        self.rows = read_scl(self.files["scl"])
        self.row_by_y = {}
        for r in self.rows:
            self.row_by_y.setdefault(round(r["y"], 6), r)
        # fixed: terminal in .nodes or /FIXED tag in the input .pl
        for name, n in self.nodes.items():
            tags = [x.upper() for x in self.gp.get(name, {}).get("tags", [])]
            if "/FIXED" in tags or "/FIXED_NI" in tags:
                n["fixed"] = True
            if "/FIXED_NI" in tags:
                n["ni"] = True
        self.movable = [n for n in self.order if not self.nodes[n]["fixed"]]

    def err(self, msg):
        self.errors.append(msg)

    def check_placement(self, path, label):
        start_errors = len(self.errors)
        try:
            out = read_pl(path)
        except (OSError, ValueError, IndexError) as e:
            self.err("%s: cannot read %s: %s" % (label, path, e))
            return None
        for name in self.order:
            if name not in out:
                self.err("%s: object %s missing" % (label, name))
        for name in out:
            if name not in self.nodes:
                self.err("%s: unknown object %s" % (label, name))
        if len(self.errors) > start_errors:
            return None

        for name in self.order:
            g, o = self.gp.get(name), out[name]
            if g is None:
                continue
            if o["orient"] != g["orient"]:
                self.err("%s: %s orientation changed %s -> %s" % (label, name, g["orient"], o["orient"]))
            if o["tags"] != g["tags"]:
                self.err("%s: %s tag changed %s -> %s" % (label, name, g["tags"], o["tags"]))
            if self.nodes[name]["fixed"] and (o["xs"] != g["xs"] or o["ys"] != g["ys"]):
                self.err("%s: fixed object %s moved (%s,%s) -> (%s,%s)"
                         % (label, name, g["xs"], g["ys"], o["xs"], o["ys"]))

        # Per-row interval lists: movable cells and blocking terminals.
        per_row = {}
        for name in self.movable:
            n, o = self.nodes[name], out[name]
            x, y = o["x"], o["y"]
            row = self.row_by_y.get(round(y, 6))
            if row is None:
                self.err("%s: %s y=%g is not a row coordinate" % (label, name, y))
                continue
            if n["h"] > row["h"] + EPS:
                self.err("%s: %s height %g exceeds row height %g" % (label, name, n["h"], row["h"]))
            inside = False
            for org, ns in row["subrows"]:
                end = org + (ns - 1) * row["sp"] + row["sw"]
                if x >= org - EPS and x + n["w"] <= end + EPS:
                    k = (x - org) / row["sp"]
                    if abs(k - round(k)) > EPS:
                        self.err("%s: %s x=%g not site aligned (origin %g, spacing %g)"
                                 % (label, name, x, org, row["sp"]))
                    inside = True
                    break
            if not inside:
                self.err("%s: %s [%g,%g) at y=%g not inside a single subrow"
                         % (label, name, x, x + n["w"], y))
            per_row.setdefault(row["y"], []).append((x, x + n["w"], name, False))

        for name in self.order:
            n = self.nodes[name]
            if not n["fixed"] or n["ni"] or name not in self.gp:
                continue
            g = self.gp[name]
            tx0, tx1 = g["x"], g["x"] + n["w"]
            ty0, ty1 = g["y"], g["y"] + n["h"]
            if tx1 - tx0 <= EPS or ty1 - ty0 <= EPS:
                continue
            for r in self.rows:
                if min(ty1, r["y"] + r["h"]) - max(ty0, r["y"]) > EPS:
                    per_row.setdefault(r["y"], []).append((tx0, tx1, name, True))

        overlaps = 0
        for y, items in per_row.items():
            items.sort()
            # Sweep keeping the furthest-reaching interval so far.
            reach = None
            for it in items:
                if reach is not None and it[0] < reach[1] - EPS and not (it[3] and reach[3]):
                    overlaps += 1
                    if overlaps <= 20:
                        self.err("%s: overlap at y=%g between %s [%g,%g) and %s [%g,%g)"
                                 % (label, y, reach[2], reach[0], reach[1], it[2], it[0], it[1]))
                if reach is None or it[1] > reach[1]:
                    reach = it
        if overlaps > 20:
            self.err("%s: ... %d overlaps in total" % (label, overlaps))
        return out

    def displacement(self, out):
        tot = mx = 0.0
        for name in self.movable:
            g, o = self.gp[name], out[name]
            d = abs(o["x"] - g["x"]) + abs(o["y"] - g["y"])
            tot += d
            mx = max(mx, d)
        return tot, mx

    def hpwl(self, out):
        total = 0.0
        for net in self.nets:
            if len(net) < 2:
                continue
            xs = [out[p]["x"] + self.nodes[p]["w"] / 2 for p in net]
            ys = [out[p]["y"] + self.nodes[p]["h"] / 2 for p in net]
            total += max(xs) - min(xs) + max(ys) - min(ys)
        return total

    def check_copies(self, out_base):
        aux = out_base + ".aux"
        bn = os.path.basename(out_base)
        if not os.path.exists(aux):
            self.err("missing %s" % aux)
        else:
            names = [tok for t in lines_of(aux) for tok in t if "." in tok]
            for ext in ("nodes", "nets", "pl", "scl"):
                if bn + "." + ext not in names:
                    self.err("%s does not reference %s.%s" % (aux, bn, ext))
        for ext in ("nodes", "nets", "scl", "wts"):
            src, dst = self.files[ext], out_base + "." + ext
            if not os.path.exists(src):
                continue
            if not os.path.exists(dst):
                self.err("missing %s" % dst)
                continue
            with open(src, "rb") as a, open(dst, "rb") as b:
                if a.read() != b.read():
                    self.err("%s differs from %s" % (dst, src))


STDOUT_RE = [
    ("Total displacement (legal)", re.compile(r"^Total displacement \(legal\): (-?\d+(\.\d+)?)$")),
    ("Maximum displacement (legal)", re.compile(r"^Maximum displacement \(legal\): (-?\d+(\.\d+)?)$")),
    ("Total HPWL (legal)", re.compile(r"^Total HPWL \(legal\): (-?\d+(\.\d+)?)$")),
    ("Total HPWL (SA)", re.compile(r"^Total HPWL \(SA\): (-?\d+(\.\d+)?)$")),
]


def close(a, b):
    return abs(a - b) <= 1e-3 + 1e-9 * max(abs(a), abs(b))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input_base")
    ap.add_argument("output_base")
    ap.add_argument("--stdout", help="file holding dp's stdout to compare against")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    ck = Checker(a.input_base, a.quiet)
    legal = ck.check_placement(a.output_base + "_legal.pl", "legal")
    final = ck.check_placement(a.output_base + ".pl", "SA")
    ck.check_copies(a.output_base)

    vals = {}
    if legal is not None:
        vals["Total displacement (legal)"], vals["Maximum displacement (legal)"] = ck.displacement(legal)
        vals["Total HPWL (legal)"] = ck.hpwl(legal)
    if final is not None:
        vals["Total HPWL (SA)"] = ck.hpwl(final)
        if legal is not None and vals["Total HPWL (SA)"] > vals["Total HPWL (legal)"] + EPS:
            ck.err("SA HPWL %.3f is worse than legal HPWL %.3f"
                   % (vals["Total HPWL (SA)"], vals["Total HPWL (legal)"]))

    if a.stdout:
        with open(a.stdout) as f:
            lines = f.read().splitlines()
        if len(lines) != 4:
            ck.err("stdout has %d lines, expected 4" % len(lines))
        for i, (key, rx) in enumerate(STDOUT_RE):
            if i >= len(lines):
                break
            m = rx.match(lines[i])
            if not m:
                ck.err("stdout line %d malformed: %r" % (i + 1, lines[i]))
            elif key in vals and not close(float(m.group(1)), vals[key]):
                ck.err("stdout %s = %s but checker computes %.3f" % (key, m.group(1), vals[key]))

    for key, _ in STDOUT_RE:
        if key in vals:
            print("%-30s %.3f" % (key + ":", vals[key]))
    if ck.errors:
        for e in ck.errors[:40]:
            print("FAIL:", e)
        if len(ck.errors) > 40:
            print("FAIL: ... %d more" % (len(ck.errors) - 40))
        print("RESULT: FAIL (%d problems)" % len(ck.errors))
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

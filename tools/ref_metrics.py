#!/usr/bin/env python3
"""Displacement and HPWL of a reference placement against a case's input.

Usage: ref_metrics.py IN_BASE REF_PL
Prints "D_total,D_max,HPWL" of REF_PL measured from IN_BASE.pl (the global
placement). Used by run_all.sh for the reference columns.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from checker import Checker, read_pl  # noqa: E402


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ck = Checker(sys.argv[1])
    ref = read_pl(sys.argv[2])
    total, mx = ck.displacement(ref)
    print("%.3f,%.3f,%.3f" % (total, mx, ck.hpwl(ref)))


if __name__ == "__main__":
    main()

#pragma once

#include <string>
#include <vector>

#include "Design.h"

// A maximal obstacle-free piece of one subrow. Positions are site indices k
// with x = org + k*sp; a cell of ws sites at k is legal iff
// kLo <= k and k + ws <= kHi.
struct Segment {
    int row = 0;
    double y = 0;
    double org = 0, sp = 1;
    long long kLo = 0, kHi = 0;

    double xAt(long long k) const { return org + static_cast<double>(k) * sp; }
    double xLo() const { return xAt(kLo); }
    double xHi() const { return xAt(kHi); }
    long long capacity() const { return kHi - kLo; }
};

struct Layout {
    std::vector<Segment> segs;        // ordered by row, then x
    std::vector<int> rowFirstSeg;     // segments of row r: [rowFirstSeg[r], rowFirstSeg[r+1])
    std::vector<double> rowY;         // sorted ascending (same order as Design::rows)

    int numRows() const { return static_cast<int>(rowY.size()); }
    // Index of the row whose y is closest to y.
    int nearestRow(double y) const;
    // Width of cell in sites of segment s (rounded up).
    static long long sitesOf(double w, const Segment& s);
};

// Cuts every subrow by the fixed, non-NI objects overlapping its row.
void buildLayout(const Design& d, Layout& L);

// Full legality check of the movable cells in p. Returns the number of
// violations; the first few are described in msg.
long long verifyLegal(const Design& d, const Layout& L, const Placement& p, std::string& msg);

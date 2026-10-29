#include "Layout.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr double kEps = 1e-6;
}

int Layout::nearestRow(double y) const {
    auto it = std::lower_bound(rowY.begin(), rowY.end(), y);
    int r = static_cast<int>(it - rowY.begin());
    if (r >= numRows()) return numRows() - 1;
    if (r > 0 && y - rowY[r - 1] < rowY[r] - y) return r - 1;
    return r;
}

long long Layout::sitesOf(double w, const Segment& s) {
    return static_cast<long long>(std::ceil(w / s.sp - kEps));
}

void buildLayout(const Design& d, Layout& L) {
    const int nr = static_cast<int>(d.rows.size());
    L.rowY.resize(nr);
    for (int r = 0; r < nr; ++r) L.rowY[r] = d.rows[r].y;

    // Blocking x-intervals per row.
    double maxRowH = 0;
    for (const Row& row : d.rows) maxRowH = std::max(maxRowH, row.h);
    std::vector<std::vector<std::pair<double, double>>> blocks(nr);
    for (const Cell& c : d.cells) {
        if (!c.fixed || c.ni || c.w <= kEps || c.h <= kEps) continue;
        double y0 = c.gy, y1 = c.gy + c.h;
        // Candidate rows: y < y1 and y + h > y0 (so y > y0 - maxRowH).
        int hi = static_cast<int>(std::lower_bound(L.rowY.begin(), L.rowY.end(), y1) - L.rowY.begin());
        for (int r = hi - 1; r >= 0 && L.rowY[r] > y0 - maxRowH; --r) {
            const Row& row = d.rows[r];
            if (std::min(y1, row.y + row.h) - std::max(y0, row.y) > kEps)
                blocks[r].emplace_back(c.gx, c.gx + c.w);
        }
    }

    L.segs.clear();
    L.rowFirstSeg.assign(nr + 1, 0);
    for (int r = 0; r < nr; ++r) {
        L.rowFirstSeg[r] = static_cast<int>(L.segs.size());
        const Row& row = d.rows[r];
        auto& bl = blocks[r];
        std::sort(bl.begin(), bl.end());
        for (const Subrow& sr : row.subrows) {
            double sx0 = sr.origin;
            double sx1 = sr.origin + static_cast<double>(sr.numSites - 1) * row.siteSp + row.siteW;
            // Free intervals of [sx0, sx1) minus the blocks.
            std::vector<std::pair<double, double>> freeIv;
            double cur = sx0;
            for (const auto& b : bl) {
                if (b.second <= cur + kEps) continue;
                if (b.first >= sx1 - kEps) break;
                if (b.first > cur + kEps) freeIv.emplace_back(cur, std::min(b.first, sx1));
                cur = std::max(cur, b.second);
                if (cur >= sx1 - kEps) break;
            }
            if (cur < sx1 - kEps) freeIv.emplace_back(cur, sx1);
            for (const auto& iv : freeIv) {
                Segment s;
                s.row = r;
                s.y = row.y;
                s.org = sr.origin;
                s.sp = row.siteSp;
                s.kLo = static_cast<long long>(std::ceil((iv.first - s.org) / s.sp - kEps));
                s.kHi = static_cast<long long>(std::floor((iv.second - s.org) / s.sp + kEps));
                if (s.kHi > s.kLo) L.segs.push_back(s);
            }
        }
        std::sort(L.segs.begin() + L.rowFirstSeg[r], L.segs.end(),
                  [](const Segment& a, const Segment& b) { return a.xLo() < b.xLo(); });
    }
    L.rowFirstSeg[nr] = static_cast<int>(L.segs.size());
}

long long verifyLegal(const Design& d, const Layout& L, const Placement& p, std::string& msg) {
    long long bad = 0;
    auto report = [&](const std::string& s) {
        if (++bad <= 5) msg += s + "\n";
    };
    std::vector<std::vector<std::pair<double, int>>> perSeg(L.segs.size());
    for (int id : d.movable) {
        const Cell& c = d.cells[id];
        double x = p.x[id], y = p.y[id];
        int r = L.nearestRow(y);
        if (r < 0 || std::fabs(L.rowY[r] - y) > kEps) { report(c.name + " not on a row"); continue; }
        int found = -1;
        for (int s = L.rowFirstSeg[r]; s < L.rowFirstSeg[r + 1]; ++s) {
            const Segment& g = L.segs[s];
            if (x >= g.xLo() - kEps && x + c.w <= g.xHi() + kEps) { found = s; break; }
        }
        if (found < 0) { report(c.name + " not inside a free segment"); continue; }
        const Segment& g = L.segs[found];
        double k = (x - g.org) / g.sp;
        if (std::fabs(k - std::round(k)) > kEps) { report(c.name + " not site aligned"); continue; }
        perSeg[found].emplace_back(x, id);
    }
    for (auto& v : perSeg) {
        std::sort(v.begin(), v.end());
        for (size_t i = 1; i < v.size(); ++i) {
            const Cell& a = d.cells[v[i - 1].second];
            if (v[i - 1].first + a.w > v[i].first + kEps)
                report(a.name + " overlaps " + d.cells[v[i].second].name);
        }
    }
    return bad;
}

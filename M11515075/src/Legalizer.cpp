#include "Legalizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <queue>

#include "Timer.h"

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kEps = 1e-9;
constexpr int kMaxRounds = 8;             // post-pass rounds in Stage 1
constexpr double kMinRoundGain = 3e-3;    // stop when a round gains < 0.3% D_total
constexpr double kMinRoundMaxGain = 1e-2; // ... and < 1% D_max

// Abacus cluster (Spindler et al., ISPD'08) in site units of its segment.
struct Cluster {
    double e = 0, q = 0;   // total weight, weighted target sum
    long long w = 0;       // width in sites
    double x = 0;          // optimal (clamped) start
};

struct AbacusSeg {
    std::vector<int> cells;        // insertion order == left-to-right order
    std::vector<Cluster> cl;
    std::vector<int> clFirst;      // index into cells of each cluster's first cell
    long long used = 0;
};

class Legalizer {
public:
    Legalizer(const Design& d, const Layout& L, const LegalizerOptions& o)
        : d_(d), L_(L), opt_(o),
          seg_(d.cells.size(), -1), k_(d.cells.size(), 0), segCells_(L.segs.size()) {}

    bool run(Placement& out, LegalizerTrace& trace, std::string& err);

private:
    // ---- Abacus ----
    bool abacusAll(const std::vector<int>& order, std::vector<int>& failed);
    double trial(const AbacusSeg& s, const Segment& g, int id, double& xSites) const;
    void commit(AbacusSeg& s, const Segment& g, int id);
    int bestSegment(int id, const std::vector<AbacusSeg>& as) const;

    // ---- shared helpers ----
    double target(int id, const Segment& g) const { return (d_.cells[id].gx - g.org) / g.sp; }
    long long ws(int id, const Segment& g) const { return Layout::sitesOf(d_.cells[id].w, g); }
    double dispAt(int id, int s, long long k) const {
        const Segment& g = L_.segs[s];
        const Cell& c = d_.cells[id];
        return std::fabs(g.xAt(k) - c.gx) + std::fabs(g.y - c.gy);
    }
    double disp(int id) const { return dispAt(id, seg_[id], k_[id]); }
    Displacement measure() const;

    // ---- L1 reposition ----
    void l1All();
    void l1Segment(int s);

    // ---- post-pass ----
    void removeFromSeg(int id);
    void insertIntoSeg(int id, int s, long long k);
    bool tryGapMove(int id);
    enum class SwapGoal { kMax, kTotal };
    int trySwap(int id, SwapGoal goal, double cap);
    void reduceMax();
    void reduceTotal(double cap);

    template <class F>
    void forRowsNear(double gy, F&& f) const;

    const Design& d_;
    const Layout& L_;
    const LegalizerOptions& opt_;
    std::vector<int> seg_;
    std::vector<long long> k_;
    std::vector<std::vector<int>> segCells_;   // sorted by k
};

// Visits rows in order of increasing |row.y - gy| until f(r, dy) returns
// false.
template <class F>
void Legalizer::forRowsNear(double gy, F&& f) const {
    int nr = L_.numRows();
    int up = static_cast<int>(std::lower_bound(L_.rowY.begin(), L_.rowY.end(), gy) - L_.rowY.begin());
    int down = up - 1;
    while (true) {
        double dyU = up < nr ? std::fabs(L_.rowY[up] - gy) : kInf;
        double dyD = down >= 0 ? std::fabs(L_.rowY[down] - gy) : kInf;
        int r;
        double dy;
        if (dyU <= dyD) { r = up++; dy = dyU; } else { r = down--; dy = dyD; }
        if (dy == kInf || !f(r, dy)) break;
    }
}

// ------------------------------------------------------------------ Abacus

double Legalizer::trial(const AbacusSeg& s, const Segment& g, int id, double& xSites) const {
    long long wc = ws(id, g);
    double e = 1, q = target(id, g);
    long long w = wc;
    int idx = static_cast<int>(s.cl.size()) - 1;
    double x;
    for (;;) {
        x = std::min(std::max(q / e, static_cast<double>(g.kLo)), static_cast<double>(g.kHi - w));
        if (idx >= 0 && s.cl[idx].x + static_cast<double>(s.cl[idx].w) > x) {
            const Cluster& p = s.cl[idx];
            q = p.q + q - e * static_cast<double>(p.w);
            e += p.e;
            w += p.w;
            --idx;
        } else {
            break;
        }
    }
    xSites = x + static_cast<double>(w - wc);
    const Cell& c = d_.cells[id];
    return std::fabs(g.org + xSites * g.sp - c.gx) + std::fabs(g.y - c.gy);
}

void Legalizer::commit(AbacusSeg& s, const Segment& g, int id) {
    long long wc = ws(id, g);
    Cluster c;
    c.e = 1;
    c.q = target(id, g);
    c.w = wc;
    s.clFirst.push_back(static_cast<int>(s.cells.size()));
    s.cells.push_back(id);
    s.used += wc;
    for (;;) {
        c.x = std::min(std::max(c.q / c.e, static_cast<double>(g.kLo)), static_cast<double>(g.kHi - c.w));
        if (!s.cl.empty() && s.cl.back().x + static_cast<double>(s.cl.back().w) > c.x) {
            Cluster p = s.cl.back();
            s.cl.pop_back();
            int first = s.clFirst[s.clFirst.size() - 2];
            s.clFirst.pop_back();
            s.clFirst.back() = first;
            p.q += c.q - c.e * static_cast<double>(p.w);
            p.e += c.e;
            p.w += c.w;
            c = p;
        } else {
            break;
        }
    }
    s.cl.push_back(c);
}

int Legalizer::bestSegment(int id, const std::vector<AbacusSeg>& as) const {
    const Cell& c = d_.cells[id];
    double best = kInf;
    int bestSeg = -1;
    forRowsNear(c.gy, [&](int r, double dy) {
        if (dy >= best) return false;
        int a = L_.rowFirstSeg[r], b = L_.rowFirstSeg[r + 1];
        if (a == b) return true;
        // First segment whose left edge is right of gx; start just before it.
        int s0 = static_cast<int>(std::upper_bound(L_.segs.begin() + a, L_.segs.begin() + b, c.gx,
                                                   [](double x, const Segment& g) { return x < g.xLo(); }) -
                                  L_.segs.begin());
        auto visit = [&](int s) {
            const Segment& g = L_.segs[s];
            long long wc = ws(id, g);
            if (as[s].used + wc > g.capacity()) return;
            double xs;
            double cost = trial(as[s], g, id, xs);
            if (cost < best) { best = cost; bestSeg = s; }
        };
        for (int s = s0; s < b; ++s) {   // segments right of gx
            const Segment& g = L_.segs[s];
            if (dy + std::max(0.0, g.xLo() - c.gx) >= best) break;
            visit(s);
        }
        for (int s = s0 - 1; s >= a; --s) {   // segments containing / left of gx
            const Segment& g = L_.segs[s];
            if (dy + std::max(0.0, c.gx + c.w - g.xHi()) >= best) break;
            visit(s);
        }
        return true;
    });
    return bestSeg;
}

bool Legalizer::abacusAll(const std::vector<int>& order, std::vector<int>& failed) {
    std::vector<AbacusSeg> as(L_.segs.size());
    failed.clear();
    for (int id : order) {
        int s = bestSegment(id, as);
        if (s < 0) { failed.push_back(id); continue; }
        commit(as[s], L_.segs[s], id);
    }
    if (!failed.empty()) return false;
    // Snap clusters to sites. Rounding is monotone and widths are whole
    // sites, so non-overlap and segment bounds are preserved.
    for (size_t s = 0; s < as.size(); ++s) {
        const Segment& g = L_.segs[s];
        AbacusSeg& a = as[s];
        segCells_[s] = a.cells;
        for (size_t c = 0; c < a.cl.size(); ++c) {
            long long k = std::llround(a.cl[c].x);
            int end = c + 1 < a.cl.size() ? a.clFirst[c + 1] : static_cast<int>(a.cells.size());
            for (int i = a.clFirst[c]; i < end; ++i) {
                int id = a.cells[i];
                seg_[id] = static_cast<int>(s);
                k_[id] = k;
                k += ws(id, g);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------- L1 reposition

// Two-heap median holder for pool-adjacent-violators blocks.
struct MedianBlock {
    std::vector<double> lo;   // max-heap
    std::vector<double> hi;   // min-heap
    int first = 0, last = 0;  // cell index range [first, last)

    double median() const { return lo.front(); }
    size_t size() const { return lo.size() + hi.size(); }
    void insert(double v) {
        if (lo.empty() || v <= lo.front()) {
            lo.push_back(v);
            std::push_heap(lo.begin(), lo.end());
        } else {
            hi.push_back(v);
            std::push_heap(hi.begin(), hi.end(), std::greater<double>());
        }
        if (lo.size() > hi.size() + 1) {
            std::pop_heap(lo.begin(), lo.end());
            hi.push_back(lo.back());
            lo.pop_back();
            std::push_heap(hi.begin(), hi.end(), std::greater<double>());
        } else if (hi.size() > lo.size()) {
            std::pop_heap(hi.begin(), hi.end(), std::greater<double>());
            lo.push_back(hi.back());
            hi.pop_back();
            std::push_heap(lo.begin(), lo.end());
        }
    }
    // Absorbs the block to the right of this one (smaller into larger).
    void absorb(MedianBlock& r) {
        if (r.size() > size()) {
            std::swap(lo, r.lo);
            std::swap(hi, r.hi);
        }
        for (double v : r.lo) insert(v);
        for (double v : r.hi) insert(v);
        r.lo.clear();
        r.hi.clear();
        last = r.last;
    }
};

void Legalizer::l1Segment(int s) {
    std::vector<int>& v = segCells_[s];
    if (v.empty()) return;
    const Segment& g = L_.segs[s];
    const int n = static_cast<int>(v.size());
    std::vector<long long> W(n + 1, 0);   // prefix widths in sites
    for (int i = 0; i < n; ++i) W[i + 1] = W[i] + ws(v[i], g);

    // Minimize sum |z_i - a_i| with z non-decreasing, where x_i = z_i + W_i.
    std::vector<MedianBlock> st;
    st.reserve(n);
    for (int i = 0; i < n; ++i) {
        MedianBlock b;
        b.first = i;
        b.last = i + 1;
        b.insert(target(v[i], g) - static_cast<double>(W[i]));
        st.push_back(std::move(b));
        while (st.size() >= 2 && st[st.size() - 2].median() > st.back().median()) {
            st[st.size() - 2].absorb(st.back());
            st.pop_back();
        }
    }
    std::vector<long long> nk(n);
    const double zHi = static_cast<double>(g.kHi - W[n]);
    for (const MedianBlock& b : st) {
        double z = std::min(std::max(b.median(), static_cast<double>(g.kLo)), zHi);
        long long zi = std::llround(z);
        for (int i = b.first; i < b.last; ++i) nk[i] = zi + W[i];
    }

    double oldMax = 0, newMax = 0, oldTot = 0, newTot = 0;
    for (int i = 0; i < n; ++i) {
        double o = dispAt(v[i], s, k_[v[i]]), m = dispAt(v[i], s, nk[i]);
        oldTot += o; newTot += m;
        oldMax = std::max(oldMax, o); newMax = std::max(newMax, m);
    }
    // Only accept when the segment's worst displacement does not grow.
    if (newMax <= oldMax + kEps && newTot <= oldTot + kEps)
        for (int i = 0; i < n; ++i) k_[v[i]] = nk[i];
}

void Legalizer::l1All() {
    for (size_t s = 0; s < segCells_.size(); ++s) l1Segment(static_cast<int>(s));
}

// --------------------------------------------------------------- post-pass

void Legalizer::removeFromSeg(int id) {
    std::vector<int>& v = segCells_[seg_[id]];
    auto it = std::lower_bound(v.begin(), v.end(), k_[id],
                               [&](int a, long long k) { return k_[a] < k; });
    v.erase(it);
    seg_[id] = -1;
}

void Legalizer::insertIntoSeg(int id, int s, long long k) {
    std::vector<int>& v = segCells_[s];
    auto it = std::lower_bound(v.begin(), v.end(), k,
                               [&](int a, long long kk) { return k_[a] < kk; });
    seg_[id] = s;
    k_[id] = k;
    v.insert(it, id);
}

// Moves id into the free gap with the smallest displacement if that is
// strictly better than its current one.
bool Legalizer::tryGapMove(int id) {
    const Cell& c = d_.cells[id];
    int s0 = seg_[id];
    long long k0 = k_[id];
    double cur = disp(id);
    removeFromSeg(id);

    double best = cur - 1e-6;
    int bestSeg = -1;
    long long bestK = 0;
    forRowsNear(c.gy, [&](int r, double dy) {
        if (dy >= best) return false;
        for (int s = L_.rowFirstSeg[r]; s < L_.rowFirstSeg[r + 1]; ++s) {
            const Segment& g = L_.segs[s];
            if (dy + std::max({0.0, g.xLo() - c.gx, c.gx + c.w - g.xHi()}) >= best) continue;
            const std::vector<int>& v = segCells_[s];
            long long wc = ws(id, g);
            double t = target(id, g);
            auto evalGap = [&](long long gl, long long gr) {
                if (gr - gl < wc) return;
                long long kf = static_cast<long long>(std::floor(t));
                for (long long kk : {kf, kf + 1}) {
                    kk = std::min(std::max(kk, gl), gr - wc);
                    double cost = std::fabs(g.xAt(kk) - c.gx) + dy;
                    if (cost < best) { best = cost; bestSeg = s; bestK = kk; }
                }
            };
            auto gapLeft = [&](int p) { return p == 0 ? g.kLo : k_[v[p - 1]] + ws(v[p - 1], g); };
            auto gapRight = [&](int p) { return p == static_cast<int>(v.size()) ? g.kHi : k_[v[p]]; };
            // Gap p lies between v[p-1] and v[p].
            int p0 = static_cast<int>(std::upper_bound(v.begin(), v.end(), t,
                                                       [&](double tt, int a) { return tt < k_[a]; }) -
                                      v.begin());
            for (int p = p0; p <= static_cast<int>(v.size()); ++p) {
                long long gl = gapLeft(p);
                if (dy + std::max(0.0, g.xAt(gl) - c.gx) >= best) break;
                evalGap(gl, gapRight(p));
            }
            for (int p = p0 - 1; p >= 0; --p) {
                long long gr = gapRight(p);
                if (dy + std::max(0.0, c.gx + c.w - g.xAt(gr)) >= best) break;
                evalGap(gapLeft(p), gr);
            }
        }
        return true;
    });
    if (bestSeg < 0) {
        insertIntoSeg(id, s0, k0);
        return false;
    }
    insertIntoSeg(id, bestSeg, bestK);
    return true;
}

// Swaps id with an equal-width cell. kMax: lower the larger of the two
// displacements below id's without growing the pair's total by more than
// that reduction. kTotal: lower the pair's total while keeping both at or
// below cap. Returns the partner, or -1.
int Legalizer::trySwap(int id, SwapGoal goal, double cap) {
    const Cell& a = d_.cells[id];
    double da = disp(id);
    const Segment& ga = L_.segs[seg_[id]];
    double xa = ga.xAt(k_[id]), ya = ga.y;
    int bestB = -1;
    double bestMax = da - 1e-6, bestSum = kInf, bestGain = 1e-6;
    forRowsNear(a.gy, [&](int r, double dy) {
        if (dy >= da) return false;
        for (int s = L_.rowFirstSeg[r]; s < L_.rowFirstSeg[r + 1]; ++s) {
            const Segment& g = L_.segs[s];
            const std::vector<int>& v = segCells_[s];
            double lim = da - dy;
            long long kLo = static_cast<long long>(std::floor((a.gx - lim - g.org) / g.sp));
            auto it = std::lower_bound(v.begin(), v.end(), kLo,
                                       [&](int c, long long k) { return k_[c] < k; });
            for (; it != v.end(); ++it) {
                int b = *it;
                double xb = g.xAt(k_[b]);
                if (xb > a.gx + lim) break;
                if (b == id || std::fabs(d_.cells[b].w - a.w) > kEps) continue;
                const Cell& cb = d_.cells[b];
                double na = std::fabs(xb - a.gx) + std::fabs(g.y - a.gy);
                double nb = std::fabs(xa - cb.gx) + std::fabs(ya - cb.gy);
                double db = disp(b);
                double m = std::max(na, nb);
                if (goal == SwapGoal::kMax) {
                    if (m >= bestMax && !(m == bestMax && na + nb < bestSum)) continue;
                    if ((na + nb) - (da + db) > da - m) continue;
                    bestMax = m;
                    bestSum = na + nb;
                } else {
                    double gain = (da + db) - (na + nb);
                    if (gain <= bestGain || m > cap) continue;
                    bestGain = gain;
                }
                bestB = b;
            }
        }
        return true;
    });
    if (bestB < 0) return -1;
    int sa = seg_[id], sb = seg_[bestB];
    long long ka = k_[id], kb = k_[bestB];
    // Locate both slots before writing: sa may equal sb.
    std::vector<int>& va = segCells_[sa];
    std::vector<int>& vb = segCells_[sb];
    auto ia = std::find(va.begin(), va.end(), id);
    auto ib = std::find(vb.begin(), vb.end(), bestB);
    *ia = bestB;
    *ib = id;
    seg_[id] = sb; k_[id] = kb;
    seg_[bestB] = sa; k_[bestB] = ka;
    return bestB;
}

// Keeps improving the currently worst cell until it cannot move.
void Legalizer::reduceMax() {
    using Item = std::pair<double, int>;
    std::priority_queue<Item> pq;
    for (int id : d_.movable) pq.emplace(disp(id), id);
    long long iter = 0;
    while (!pq.empty()) {
        if ((++iter & 63) == 0 && Timer::elapsed() > opt_.deadline) break;
        auto [dv, id] = pq.top();
        pq.pop();
        if (std::fabs(dv - disp(id)) > kEps) continue;   // stale entry
        int partner = -1;
        bool moved = tryGapMove(id);
        if (!moved) {
            partner = trySwap(id, SwapGoal::kMax, kInf);
            moved = partner >= 0;
        }
        if (!moved) break;   // the worst cell cannot improve: D_max is settled
        pq.emplace(disp(id), id);
        if (partner >= 0) pq.emplace(disp(partner), partner);
    }
}

// Every cell, worst first, tries a strictly better free gap, then a swap
// that lowers the pair's total without exceeding cap.
void Legalizer::reduceTotal(double cap) {
    std::vector<int> order(d_.movable);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return disp(a) > disp(b); });
    for (size_t i = 0; i < order.size(); ++i) {
        if ((i & 255) == 0 && Timer::elapsed() > opt_.deadline) break;
        if (disp(order[i]) <= 0) break;
        if (!tryGapMove(order[i])) trySwap(order[i], SwapGoal::kTotal, cap);
    }
}

// ----------------------------------------------------------------- driver

Displacement Legalizer::measure() const {
    Displacement r;
    for (int id : d_.movable) {
        double v = disp(id);
        r.total += v;
        r.max = std::max(r.max, v);
    }
    return r;
}

bool Legalizer::run(Placement& out, LegalizerTrace& trace, std::string& err) {
    std::vector<int> order(d_.movable);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Cell& ca = d_.cells[a];
        const Cell& cb = d_.cells[b];
        if (ca.gx != cb.gx) return ca.gx < cb.gx;
        return a < b;
    });

    std::vector<int> failed;
    bool ok = abacusAll(order, failed);
    // Fallbacks for fragmented capacity: place the cells that failed first,
    // then switch to widest-first (first-fit decreasing style) ordering.
    for (int attempt = 0; !ok && attempt < 3; ++attempt) {
        if (opt_.verbose)
            std::fprintf(stderr, "  abacus: %zu cells unplaced, retry %d\n", failed.size(), attempt + 1);
        std::vector<char> isFailed(d_.cells.size(), 0);
        for (int id : failed) isFailed[id] = 1;
        auto wider = [&](int a, int b) {
            if (d_.cells[a].w != d_.cells[b].w) return d_.cells[a].w > d_.cells[b].w;
            return d_.cells[a].gx < d_.cells[b].gx;
        };
        if (attempt < 2) {
            std::stable_partition(order.begin(), order.end(), [&](int id) { return isFailed[id] != 0; });
            std::sort(order.begin(), order.begin() + static_cast<long>(failed.size()), wider);
        } else {
            std::sort(order.begin(), order.end(), wider);
        }
        ok = abacusAll(order, failed);
    }
    if (!ok) {
        err = "cannot legalize: " + std::to_string(failed.size()) +
              " cells do not fit into the free row segments";
        return false;
    }
    for (auto& v : segCells_)
        std::sort(v.begin(), v.end(), [&](int a, int b) { return k_[a] < k_[b]; });
    trace.emplace_back("abacus", measure());

    if (opt_.l1Reposition) {
        l1All();
        trace.emplace_back("l1", measure());
    }
    // Improvement rounds until the gain is negligible or time runs out.
    for (int round = 1; opt_.postPass && round <= kMaxRounds; ++round) {
        Displacement before = measure();
        reduceMax();
        Displacement m = measure();
        trace.emplace_back("max#" + std::to_string(round), m);
        reduceTotal(m.max);
        trace.emplace_back("total#" + std::to_string(round), measure());
        if (opt_.l1Reposition) {
            l1All();
            trace.emplace_back("l1#" + std::to_string(round), measure());
        }
        Displacement after = measure();
        bool totalGain = after.total < before.total * (1 - kMinRoundGain);
        bool maxGain = after.max < before.max * (1 - kMinRoundMaxGain);
        if (Timer::elapsed() > opt_.deadline || (!totalGain && !maxGain)) break;
    }

    out.x.resize(d_.cells.size());
    out.y.resize(d_.cells.size());
    for (size_t i = 0; i < d_.cells.size(); ++i) {
        const Cell& c = d_.cells[i];
        if (c.fixed) {
            out.x[i] = c.gx;
            out.y[i] = c.gy;
        } else {
            const Segment& g = L_.segs[seg_[i]];
            out.x[i] = g.xAt(k_[i]);
            out.y[i] = g.y;
        }
    }
    return true;
}

}  // namespace

bool legalize(const Design& d, const Layout& L, const LegalizerOptions& opt, Placement& out,
              LegalizerTrace& trace, std::string& err) {
    Legalizer lg(d, L, opt);
    return lg.run(out, trace, err);
}

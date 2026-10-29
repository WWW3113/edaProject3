#include "Annealer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <random>
#include <utility>
#include <vector>

#include "Timer.h"

using namespace sa_tuning;

namespace {

constexpr double kEps = 1e-9;

// Bounding box of a net's pin centers with the number of pins on each edge.
struct Box {
    double xlo = 0, xhi = 0, ylo = 0, yhi = 0;
    int nxlo = 0, nxhi = 0, nylo = 0, nyhi = 0;
    double hpwl() const { return (xhi - xlo) + (yhi - ylo); }
};

// Adds coordinate nw and removes o on one axis. False if an edge lost its
// last pin (the box must then be recomputed).
inline bool updateAxis(double& lo, double& hi, int& nlo, int& nhi, double o, double nw) {
    if (nw < lo) { lo = nw; nlo = 1; } else if (nw == lo) { ++nlo; }
    if (nw > hi) { hi = nw; nhi = 1; } else if (nw == hi) { ++nhi; }
    if (o == lo) --nlo;
    if (o == hi) --nhi;
    return nlo > 0 && nhi > 0;
}

struct Change {
    int id;
    int seg;
    long long k;
    double ocx, ocy;   // old center, for incremental boxes and undo
};

class Annealer {
public:
    Annealer(const Design& d, const Layout& L, const SaOptions& o)
        : d_(d), L_(L), opt_(o), rng_(o.seed) {}

    void run(const Placement& legal, Placement& best, SaResult& res);

private:
    void init(const Placement& legal);
    Box computeBox(int n) const;

    // Move proposals: fill ch_/nch_, return false if nothing to try.
    bool proposeAt(int a, double tx, double ty);
    bool proposeOptimal(int a);
    bool proposeRandom(int a);
    bool proposeAdjacentSwap(int a);
    bool proposeShift(int a);
    void freeSpan(int s, long long k, long long w, int self, long long& lo, long long& hi) const;
    void optimalRegion(int a, double& x1, double& x2, double& y1, double& y2);

    double evaluate();          // applies ch_ tentatively to centers, returns delta
    void commit(double delta);  // keeps the tentative move
    void revert();              // undoes the tentative centers

    void markDirty(int id) {
        if (!dirty_[id]) { dirty_[id] = 1; journal_.push_back(id); }
    }
    void syncBest();

    bool isFree(int s, long long k, long long ws, int self) const {
        const Segment& g = L_.segs[s];
        if (k < g.kLo || k + ws > g.kHi) return false;
        const int* o = occ_[s].data() + (k - g.kLo);
        for (long long i = 0; i < ws; ++i)
            if (o[i] >= 0 && o[i] != self) return false;
        return true;
    }
    int occAt(int s, long long k) const { return occ_[s][k - L_.segs[s].kLo]; }
    void setOcc(int s, long long k, long long ws, int v) {
        int* o = occ_[s].data() + (k - L_.segs[s].kLo);
        std::fill(o, o + ws, v);
    }
    long long ws(int id, int s) const { return Layout::sitesOf(d_.cells[id].w, L_.segs[s]); }
    int segmentNear(int r, double x) const;
    double uniform(double a, double b) { return a + (b - a) * u01_(rng_); }
    double currentTotal() const;
    void writeLog(long long iter, double T, double rate);

    const Design& d_;
    const Layout& L_;
    const SaOptions& opt_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> u01_{0.0, 1.0};

    std::vector<int> mov_;                    // movable ids
    std::vector<double> cx_, cy_, hw_, hh_;   // centers and half sizes of all cells
    std::vector<int> seg_;
    std::vector<long long> k_;
    std::vector<std::vector<int>> occ_;       // per segment site -> cell id or -1

    std::vector<int> netStart_, netPins_;     // CSR: net -> cells (degree >= 2 only)
    std::vector<int> cellNetStart_, cellNets_;
    std::vector<Box> box_;
    double cur_ = 0;

    std::vector<int> bestSeg_;
    std::vector<long long> bestK_;
    std::vector<char> dirty_;
    std::vector<int> journal_;
    double best_ = 0;

    Change ch_[2];
    int nch_ = 0;
    std::vector<int> stamp_;
    int stampId_ = 0;
    std::vector<int> touched_;
    std::vector<Box> newBox_;
    std::vector<double> scratch_;

    double window_ = 0, windowMin_ = 0, windowMax_ = 0;
    std::FILE* log_ = nullptr;
};

void Annealer::init(const Placement& legal) {
    const int n = static_cast<int>(d_.cells.size());
    cx_.resize(n); cy_.resize(n); hw_.resize(n); hh_.resize(n);
    seg_.assign(n, -1);
    k_.assign(n, 0);
    occ_.resize(L_.segs.size());
    for (size_t s = 0; s < L_.segs.size(); ++s) occ_[s].assign(L_.segs[s].capacity(), -1);
    for (int i = 0; i < n; ++i) {
        const Cell& c = d_.cells[i];
        hw_[i] = c.w * 0.5;
        hh_[i] = c.h * 0.5;
        cx_[i] = legal.x[i] + hw_[i];
        cy_[i] = legal.y[i] + hh_[i];
    }
    for (int id : d_.movable) {
        double x = legal.x[id], y = legal.y[id];
        int r = L_.nearestRow(y);
        for (int s = L_.rowFirstSeg[r]; s < L_.rowFirstSeg[r + 1]; ++s) {
            const Segment& g = L_.segs[s];
            if (x >= g.xLo() - 1e-6 && x + d_.cells[id].w <= g.xHi() + 1e-6) {
                seg_[id] = s;
                k_[id] = std::llround((x - g.org) / g.sp);
                setOcc(s, k_[id], ws(id, s), id);
                break;
            }
        }
        mov_.push_back(id);
    }

    // Nets with at least two distinct cells.
    netStart_.push_back(0);
    std::vector<int> cnt(n, 0);
    for (const Net& net : d_.nets) {
        if (net.pins.size() < 2) continue;
        for (int p : net.pins) { netPins_.push_back(p); ++cnt[p]; }
        netStart_.push_back(static_cast<int>(netPins_.size()));
    }
    const int nn = static_cast<int>(netStart_.size()) - 1;
    cellNetStart_.assign(n + 1, 0);
    for (int i = 0; i < n; ++i) cellNetStart_[i + 1] = cellNetStart_[i] + cnt[i];
    cellNets_.resize(cellNetStart_[n]);
    std::vector<int> fill(cellNetStart_.begin(), cellNetStart_.end() - 1);
    for (int e = 0; e < nn; ++e)
        for (int i = netStart_[e]; i < netStart_[e + 1]; ++i) cellNets_[fill[netPins_[i]]++] = e;
    box_.resize(nn);
    for (int e = 0; e < nn; ++e) box_[e] = computeBox(e);
    stamp_.assign(nn, 0);
    cur_ = currentTotal();

    bestSeg_ = seg_;
    bestK_ = k_;
    dirty_.assign(n, 0);
    best_ = cur_;

    double xmin = 1e300, xmax = -1e300;
    for (const Segment& g : L_.segs) { xmin = std::min(xmin, g.xLo()); xmax = std::max(xmax, g.xHi()); }
    double chipW = xmax - xmin;
    double chipH = L_.rowY.back() - L_.rowY.front() + (d_.rows.empty() ? 0 : d_.rows.back().h);
    double rowH = d_.rows.front().h;
    windowMax_ = std::max(chipW, chipH);
    windowMin_ = 2 * rowH;
    window_ = opt_.initWindowRows > 0 ? std::min(windowMax_, std::max(windowMin_, opt_.initWindowRows * rowH))
                                      : std::max(windowMin_, 0.1 * windowMax_);
}

Box Annealer::computeBox(int n) const {
    Box b;
    b.xlo = b.ylo = 1e300;
    b.xhi = b.yhi = -1e300;
    for (int i = netStart_[n]; i < netStart_[n + 1]; ++i) {
        int c = netPins_[i];
        double x = cx_[c], y = cy_[c];
        if (x < b.xlo) { b.xlo = x; b.nxlo = 1; } else if (x == b.xlo) { ++b.nxlo; }
        if (x > b.xhi) { b.xhi = x; b.nxhi = 1; } else if (x == b.xhi) { ++b.nxhi; }
        if (y < b.ylo) { b.ylo = y; b.nylo = 1; } else if (y == b.ylo) { ++b.nylo; }
        if (y > b.yhi) { b.yhi = y; b.nyhi = 1; } else if (y == b.yhi) { ++b.nyhi; }
    }
    return b;
}

double Annealer::currentTotal() const {
    double s = 0;
    for (const Box& b : box_) s += b.hpwl();
    return s;
}

// Segment of row r containing x, or the nearest one.
int Annealer::segmentNear(int r, double x) const {
    int a = L_.rowFirstSeg[r], b = L_.rowFirstSeg[r + 1];
    if (a == b) return -1;
    int s = static_cast<int>(std::upper_bound(L_.segs.begin() + a, L_.segs.begin() + b, x,
                                              [](double v, const Segment& g) { return v < g.xLo(); }) -
                             L_.segs.begin()) - 1;
    if (s < a) return a;
    if (x >= L_.segs[s].xHi() && s + 1 < b && L_.segs[s + 1].xLo() - x < x - L_.segs[s].xHi())
        return s + 1;
    return s;
}

// ------------------------------------------------------------------- moves

// Move a so that its center lands near (tx, ty): into free sites, by
// swapping with an equal-width occupant, or into the nearest free run.
bool Annealer::proposeAt(int a, double tx, double ty) {
    const Cell& c = d_.cells[a];
    int r = L_.nearestRow(ty - hh_[a]);
    int s = segmentNear(r, tx - hw_[a]);
    if (s < 0) return false;
    const Segment& g = L_.segs[s];
    long long w = ws(a, s);
    if (g.capacity() < w) return false;
    long long k = std::llround((tx - hw_[a] - g.org) / g.sp);
    k = std::min(std::max(k, g.kLo), g.kHi - w);
    if (s == seg_[a] && k == k_[a]) return false;

    if (isFree(s, k, w, a)) {
        ch_[0] = {a, s, k, 0, 0};
        nch_ = 1;
        return true;
    }
    // First foreign occupant of the target sites.
    int b = -1;
    for (long long i = k; i < k + w; ++i) {
        int o = occAt(s, i);
        if (o >= 0 && o != a) { b = o; break; }
    }
    if (b >= 0 && d_.cells[b].w == c.w) {
        ch_[0] = {a, seg_[b], k_[b], 0, 0};
        ch_[1] = {b, seg_[a], k_[a], 0, 0};
        nch_ = 2;
        return true;
    }
    // Different widths: swap if each cell fits into the other's slot (its
    // sites plus the free sites around it). Slots of the same segment must
    // not touch, so the two moves cannot collide.
    if (b >= 0) {
        int sa = seg_[a];
        long long wa_old = ws(a, sa), wb = ws(b, s);
        long long aLo, aHi, bLo, bHi;
        freeSpan(sa, k_[a], wa_old, -1, aLo, aHi);
        freeSpan(s, k_[b], wb, -1, bLo, bHi);
        long long wbInA = ws(b, sa);
        bool disjoint = sa != s || aHi < bLo || bHi < aLo;
        if (disjoint && aHi - aLo >= wbInA && bHi - bLo >= w) {
            long long ka = std::min(std::max(k, bLo), bHi - w);
            long long kb = std::min(std::max(k_[a], aLo), aHi - wbInA);
            ch_[0] = {a, s, ka, 0, 0};
            ch_[1] = {b, sa, kb, 0, 0};
            nch_ = 2;
            return true;
        }
    }
    // Nearest run of w free sites (a's own sites count as free): one linear
    // pass to each side, counting consecutive free sites.
    auto freeAt = [&](long long i) {
        int o = occAt(s, i);
        return o < 0 || o == a;
    };
    long long bestK = -1, bestDist = kMaxGapScan + 1;
    for (long long i = k, run = 0, end = std::min(g.kHi, k + w + kMaxGapScan); i < end; ++i) {
        run = freeAt(i) ? run + 1 : 0;
        if (run == w) {
            bestK = i - w + 1;
            bestDist = bestK - k;
            break;
        }
    }
    for (long long i = k + w - 1, run = 0, end = std::max(g.kLo, k - kMaxGapScan); i >= end; --i) {
        if (k - i >= bestDist) break;
        run = freeAt(i) ? run + 1 : 0;
        if (run == w) {
            bestK = i;
            break;
        }
    }
    if (bestK < 0 || (s == seg_[a] && bestK == k_[a])) return false;
    ch_[0] = {a, s, bestK, 0, 0};
    nch_ = 1;
    return true;
}

// [lo, hi): the w sites at k in segment s widened by the free sites on both
// sides (sites of `self` count as free), at most kMaxGapScan each way.
void Annealer::freeSpan(int s, long long k, long long w, int self, long long& lo, long long& hi) const {
    const Segment& g = L_.segs[s];
    auto freeAt = [&](long long i) {
        int o = occAt(s, i);
        return o < 0 || o == self;
    };
    lo = k;
    for (long long lim = std::max(g.kLo, k - kMaxGapScan); lo > lim && freeAt(lo - 1);) --lo;
    hi = k + w;
    for (long long lim = std::min(g.kHi, k + w + kMaxGapScan); hi < lim && freeAt(hi);) ++hi;
}

// Median of the per-net bounding boxes of a's nets, excluding a itself.
void Annealer::optimalRegion(int a, double& x1, double& x2, double& y1, double& y2) {
    std::vector<double>& xs = scratch_;
    xs.clear();
    std::vector<double> ys;
    ys.reserve(16);
    for (int i = cellNetStart_[a]; i < cellNetStart_[a + 1]; ++i) {
        int n = cellNets_[i];
        int deg = netStart_[n + 1] - netStart_[n];
        if (deg > kOptRegionMaxDegree) continue;
        double xl = 1e300, xh = -1e300, yl = 1e300, yh = -1e300;
        for (int p = netStart_[n]; p < netStart_[n + 1]; ++p) {
            int c = netPins_[p];
            if (c == a) continue;
            xl = std::min(xl, cx_[c]); xh = std::max(xh, cx_[c]);
            yl = std::min(yl, cy_[c]); yh = std::max(yh, cy_[c]);
        }
        xs.push_back(xl); xs.push_back(xh);
        ys.push_back(yl); ys.push_back(yh);
    }
    if (xs.empty()) {
        x1 = x2 = cx_[a];
        y1 = y2 = cy_[a];
        return;
    }
    size_t m = xs.size() / 2;
    std::nth_element(xs.begin(), xs.begin() + m, xs.end());
    x2 = xs[m];
    x1 = *std::max_element(xs.begin(), xs.begin() + m);
    std::nth_element(ys.begin(), ys.begin() + m, ys.end());
    y2 = ys[m];
    y1 = *std::max_element(ys.begin(), ys.begin() + m);
}

bool Annealer::proposeOptimal(int a) {
    double x1, x2, y1, y2;
    optimalRegion(a, x1, x2, y1, y2);
    double noise = 0.1 * window_;
    double tx = uniform(x1, x2) + uniform(-noise, noise);
    double ty = uniform(y1, y2) + uniform(-noise, noise);
    // Range limiter: do not jump further than the window.
    tx = std::min(std::max(tx, cx_[a] - window_), cx_[a] + window_);
    ty = std::min(std::max(ty, cy_[a] - window_), cy_[a] + window_);
    return proposeAt(a, tx, ty);
}

bool Annealer::proposeRandom(int a) {
    return proposeAt(a, cx_[a] + uniform(-window_, window_), cy_[a] + uniform(-window_, window_));
}

// Swap a with its right neighbour in the same segment; the pair keeps its
// span, so the result is legal.
bool Annealer::proposeAdjacentSwap(int a) {
    int s = seg_[a];
    const Segment& g = L_.segs[s];
    long long end = k_[a] + ws(a, s);
    long long lim = std::min(g.kHi, end + kMaxGapScan);
    for (long long i = end; i < lim; ++i) {
        int b = occAt(s, i);
        if (b < 0) continue;
        long long kb = k_[b], wb = ws(b, s), wa = ws(a, s);
        ch_[0] = {b, s, k_[a], 0, 0};
        ch_[1] = {a, s, kb + wb - wa, 0, 0};
        nch_ = 2;
        return true;
    }
    return false;
}

// Move a inside the free space around it: towards its optimal x half of the
// time, uniformly otherwise.
bool Annealer::proposeShift(int a) {
    int s = seg_[a];
    const Segment& g = L_.segs[s];
    long long w = ws(a, s);
    long long left = k_[a], right = k_[a] + w;
    long long loLim = std::max(g.kLo, k_[a] - kMaxGapScan);
    while (left > loLim && occAt(s, left - 1) < 0) --left;
    long long hiLim = std::min(g.kHi, right + kMaxGapScan);
    while (right < hiLim && occAt(s, right) < 0) ++right;
    if (right - left <= w) return false;
    long long k;
    if (u01_(rng_) < 0.5) {
        double x1, x2, y1, y2;
        optimalRegion(a, x1, x2, y1, y2);
        k = std::llround((uniform(x1, x2) - hw_[a] - g.org) / g.sp);
    } else {
        k = left + static_cast<long long>(u01_(rng_) * static_cast<double>(right - w - left + 1));
    }
    k = std::min(std::max(k, left), right - w);
    if (k == k_[a]) return false;
    ch_[0] = {a, s, k, 0, 0};
    nch_ = 1;
    return true;
}

// ------------------------------------------------------- evaluate / commit

double Annealer::evaluate() {
    ++stampId_;
    touched_.clear();
    for (int i = 0; i < nch_; ++i) {
        Change& c = ch_[i];
        c.ocx = cx_[c.id];
        c.ocy = cy_[c.id];
        const Segment& g = L_.segs[c.seg];
        cx_[c.id] = g.xAt(c.k) + hw_[c.id];
        cy_[c.id] = g.y + hh_[c.id];
        for (int j = cellNetStart_[c.id]; j < cellNetStart_[c.id + 1]; ++j) {
            int n = cellNets_[j];
            if (stamp_[n] != stampId_) { stamp_[n] = stampId_; touched_.push_back(n); }
        }
    }
    newBox_.resize(touched_.size());
    double delta = 0;
    for (size_t t = 0; t < touched_.size(); ++t) {
        int n = touched_[t];
        int deg = netStart_[n + 1] - netStart_[n];
        Box b;
        bool ok = false;
        if (deg >= kIncrementalMinDegree) {
            b = box_[n];
            ok = true;
            for (int i = 0; i < nch_ && ok; ++i) {
                const Change& c = ch_[i];
                // Is c.id a pin of net n? Its net list is short; scan it.
                bool member = false;
                for (int j = cellNetStart_[c.id]; j < cellNetStart_[c.id + 1]; ++j)
                    if (cellNets_[j] == n) { member = true; break; }
                if (!member) continue;
                ok = updateAxis(b.xlo, b.xhi, b.nxlo, b.nxhi, c.ocx, cx_[c.id]) &&
                     updateAxis(b.ylo, b.yhi, b.nylo, b.nyhi, c.ocy, cy_[c.id]);
            }
        }
        if (!ok) b = computeBox(n);
        newBox_[t] = b;
        delta += b.hpwl() - box_[n].hpwl();
    }
    return delta;
}

void Annealer::revert() {
    for (int i = nch_ - 1; i >= 0; --i) {
        cx_[ch_[i].id] = ch_[i].ocx;
        cy_[ch_[i].id] = ch_[i].ocy;
    }
}

void Annealer::commit(double delta) {
    for (size_t t = 0; t < touched_.size(); ++t) box_[touched_[t]] = newBox_[t];
    for (int i = 0; i < nch_; ++i) setOcc(seg_[ch_[i].id], k_[ch_[i].id], ws(ch_[i].id, seg_[ch_[i].id]), -1);
    for (int i = 0; i < nch_; ++i) {
        const Change& c = ch_[i];
        seg_[c.id] = c.seg;
        k_[c.id] = c.k;
        setOcc(c.seg, c.k, ws(c.id, c.seg), c.id);
        markDirty(c.id);
    }
    cur_ += delta;
    if (cur_ < best_ - 1e-6) syncBest();
}

// Makes the stored best placement equal to the current one by copying only
// the cells that changed since the last sync.
void Annealer::syncBest() {
    for (int id : journal_) {
        bestSeg_[id] = seg_[id];
        bestK_[id] = k_[id];
        dirty_[id] = 0;
    }
    journal_.clear();
    best_ = cur_;
}

void Annealer::writeLog(long long iter, double T, double rate) {
    if (log_) std::fprintf(log_, "%lld,%.3f,%.3f,%.6g,%.4f\n", iter, cur_, best_, T, rate);
}

// --------------------------------------------------------------- schedule

void Annealer::run(const Placement& legal, Placement& best, SaResult& res) {
    init(legal);
    res.startHpwl = cur_;
    const long long nMov = static_cast<long long>(mov_.size());
    bool timed = opt_.iters <= 0;
    auto timeUp = [&]() { return Timer::elapsed() > opt_.hardCap; };

    auto propose = [&](int a) {
        double r = u01_(rng_);
        if (r < opt_.pOptimal) return proposeOptimal(a);
        if (r < opt_.pRandom) return proposeRandom(a);
        if (r < opt_.pAdjacent) return proposeAdjacentSwap(a);
        return proposeShift(a);
    };
    std::uniform_int_distribution<long long> pick(0, nMov > 0 ? nMov - 1 : 0);

    if (nMov == 0 || box_.empty() || timeUp()) {
        res.stopReason = nMov == 0 ? "no movable cells" : (box_.empty() ? "no nets" : "no time");
    } else {
        // T0 from sampled uphill deltas; also measures the cost of a move.
        double t0 = Timer::elapsed(), sumUp = 0;
        int nUp = 0, samples = 0;
        for (int i = 0; i < 4000; ++i) {
            int a = mov_[pick(rng_)];
            if (!propose(a)) continue;
            double dlt = evaluate();
            revert();
            ++samples;
            if (dlt > kEps) { sumUp += dlt; ++nUp; }
        }
        double perMove = std::max(1e-7, (Timer::elapsed() - t0) / 4000.0);
        double T0 = nUp ? -(sumUp / nUp) / std::log(opt_.initAccept) : 1.0;
        double T = T0, Tfrozen = T0 * kFrozenTempRatio;

        long long mps = static_cast<long long>(opt_.movesPerCell * static_cast<double>(nMov));
        if (timed) {
            double budget = (opt_.hardCap - Timer::elapsed()) / perMove;
            long long fit = static_cast<long long>(budget / opt_.targetSteps);
            mps = std::max(static_cast<long long>(kMinMovesPerCellPerStep * static_cast<double>(nMov)),
                           std::min(mps, fit));
        }
        mps = std::max<long long>(mps, 100);
        long long logEvery = opt_.logEvery > 0 ? opt_.logEvery : std::max<long long>(1, mps / 10);
        if (!opt_.logPath.empty()) {
            log_ = std::fopen(opt_.logPath.c_str(), "w");
            if (log_) std::fprintf(log_, "iter,current_hpwl,best_hpwl,temperature,accept_rate\n");
        }
        if (opt_.verbose)
            std::fprintf(stderr, "  SA: T0 %.4g, %lld moves/step, ~%.2f us/move\n", T0, mps, perMove * 1e6);

        // (iteration, best) at the end of each step, for the stall test.
        std::deque<std::pair<long long, double>> bestHist;
        const long long patienceMoves = static_cast<long long>(kEarlyStopPatienceMovesPerCell * static_cast<double>(nMov));
        long long iter = 0;
        bool stop = false;
        writeLog(0, T, 1.0);
        while (!stop) {
            double stepStart = Timer::elapsed();
            // acc counts every accepted move; moved counts only those that
            // changed HPWL, so zero-delta plateau moves do not keep the
            // acceptance rate (and thus the schedule) artificially warm.
            long long tried = 0, acc = 0, moved = 0;
            for (long long m = 0; m < mps; ++m) {
                if ((iter & 1023) == 0 && timeUp()) { stop = true; res.stopReason = "time cap"; break; }
                if (!timed && iter >= opt_.iters) { stop = true; res.stopReason = "iteration budget"; break; }
                ++iter;
                int a = mov_[pick(rng_)];
                if (propose(a)) {
                    ++tried;
                    double dlt = evaluate();
                    if (dlt <= 0 || (T > 0 && u01_(rng_) < std::exp(-dlt / T))) {
                        commit(dlt);
                        ++acc;
                        if (std::fabs(dlt) > kEps) ++moved;
                    } else {
                        revert();
                    }
                }
                if (log_ && iter % logEvery == 0)
                    writeLog(iter, T, tried ? static_cast<double>(moved) / tried : 0.0);
            }
            res.moves = iter;
            res.accepted += acc;
            res.proposed += tried;
            ++res.steps;
            // Re-sum to keep floating error from accumulating.
            cur_ = currentTotal();
            if (cur_ < best_ - 1e-6) syncBest();

            double rate = tried ? static_cast<double>(moved) / tried : 0.0;
            if (opt_.verbose && (res.steps % 10 == 0 || stop))
                std::fprintf(stderr, "  step %4d T %-10.4g acc %5.3f win %-8.1f cur %.0f best %.0f [%.1fs]\n",
                             res.steps, T, rate, window_, cur_, best_, Timer::elapsed());
            if (stop) break;

            // Early stop: only when cold and best has stalled over the last
            // kEarlyStopPatience steps and at least patienceMoves moves.
            bestHist.emplace_back(iter, best_);
            while (bestHist.size() > 2 && static_cast<int>(bestHist.size()) > kEarlyStopPatience + 1 &&
                   iter - bestHist[1].first >= patienceMoves)
                bestHist.pop_front();
            bool longEnough = static_cast<int>(bestHist.size()) > kEarlyStopPatience &&
                              iter - bestHist.front().first >= patienceMoves;
            if (rate < kEarlyStopAcceptRate && longEnough &&
                bestHist.front().second - best_ < kEarlyStopRelImprove * best_) {
                res.stopReason = "converged";
                break;
            }

            // VPR-style cooling, sped up if the remaining time demands it.
            double alpha = rate > 0.96 ? 0.5 : rate > 0.8 ? 0.9 : rate > 0.15 ? 0.95 : opt_.lowAlpha;
            if (timed && T > Tfrozen) {
                double stepTime = std::max(1e-6, Timer::elapsed() - stepStart);
                double stepsLeft = (opt_.hardCap - Timer::elapsed()) / stepTime;
                if (stepsLeft > 1) alpha = std::min(alpha, std::pow(Tfrozen / T, 1.0 / stepsLeft));
            }
            T *= alpha;
            window_ = std::min(windowMax_, std::max(windowMin_, window_ * (1.0 - 0.44 + rate)));
        }
        writeLog(iter, T, 0.0);
        if (log_) std::fclose(log_);
    }

    res.bestHpwl = best_;
    best.x.resize(d_.cells.size());
    best.y.resize(d_.cells.size());
    for (size_t i = 0; i < d_.cells.size(); ++i) {
        if (d_.cells[i].fixed) {
            best.x[i] = d_.cells[i].gx;
            best.y[i] = d_.cells[i].gy;
        } else {
            const Segment& g = L_.segs[bestSeg_[i]];
            best.x[i] = g.xAt(bestK_[i]);
            best.y[i] = g.y;
        }
    }
}

}  // namespace

void anneal(const Design& d, const Layout& L, const Placement& legal, const SaOptions& opt,
            Placement& best, SaResult& res) {
    Annealer a(d, L, opt);
    a.run(legal, best, res);
}

#include "Metrics.h"

#include <algorithm>
#include <cmath>

Displacement displacement(const Design& d, const Placement& p) {
    Displacement r;
    for (int id : d.movable) {
        const Cell& c = d.cells[id];
        double di = std::fabs(p.x[id] - c.gx) + std::fabs(p.y[id] - c.gy);
        r.total += di;
        r.max = std::max(r.max, di);
    }
    return r;
}

double totalHpwl(const Design& d, const Placement& p) {
    double sum = 0;
    for (const Net& n : d.nets) {
        if (n.pins.size() < 2) continue;
        double xl = 1e300, xh = -1e300, yl = 1e300, yh = -1e300;
        for (int id : n.pins) {
            const Cell& c = d.cells[id];
            double cx = p.x[id] + c.w * 0.5, cy = p.y[id] + c.h * 0.5;
            xl = std::min(xl, cx); xh = std::max(xh, cx);
            yl = std::min(yl, cy); yh = std::max(yh, cy);
        }
        sum += (xh - xl) + (yh - yl);
    }
    return sum;
}

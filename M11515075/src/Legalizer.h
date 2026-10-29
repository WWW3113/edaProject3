#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Design.h"
#include "Layout.h"
#include "Metrics.h"

struct LegalizerOptions {
    bool l1Reposition = true;   // per-segment L1-optimal shift with fixed order
    bool postPass = true;       // gap moves / swaps for high-displacement cells
    double deadline = 15.0;     // seconds since program start for the optional passes
    bool verbose = false;
};

// Displacement after each step, for reporting.
using LegalizerTrace = std::vector<std::pair<std::string, Displacement>>;

// Stage 1. Fills out with a legal placement (fixed objects at their input
// positions). Returns false with err set when the cells cannot all be placed.
bool legalize(const Design& d, const Layout& L, const LegalizerOptions& opt, Placement& out,
              LegalizerTrace& trace, std::string& err);

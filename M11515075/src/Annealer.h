#pragma once

#include <cstdint>
#include <string>

#include "Design.h"
#include "Layout.h"

// SA tuning knobs, kept together so they are easy to adjust.
namespace sa_tuning {
constexpr double kHardCapSec = 45.0;            // SA stops by this time since program start
constexpr double kEarlyStopAcceptRate = 0.05;   // early stop only once acceptance is below this
constexpr double kEarlyStopRelImprove = 1e-4;   // ... and best improved < 0.01% ...
constexpr int kEarlyStopPatience = 5;           // ... over this many temperature steps
constexpr double kEarlyStopPatienceMovesPerCell = 10; // ... and at least this many moves per cell
constexpr double kInitAcceptProb = 0.01;        // uphill acceptance targeted by T0
constexpr double kInitWindowRows = 0;           // initial range window in rows (0: 10% of chip)
constexpr double kLowAcceptAlpha = 0.95;        // cooling factor once acceptance < 15%
constexpr double kMovesPerCellPerStep = 20;     // moves per temperature step, per movable cell
constexpr double kMinMovesPerCellPerStep = 0.5; // lower bound when time is short
constexpr int kTargetSteps = 150;               // steps the time budget should allow
constexpr double kFrozenTempRatio = 1e-4;       // T/T0 regarded as frozen for scheduling
constexpr int kOptRegionMaxDegree = 32;         // larger nets are skipped for the optimal region
constexpr int kIncrementalMinDegree = 12;       // nets at least this big use edge counts
constexpr int kMaxGapScan = 256;                // sites scanned when looking for neighbours
// Move mix (cumulative thresholds of a uniform draw).
constexpr double kPOptimal = 0.40;   // move/swap toward the optimal region
constexpr double kPRandom = 0.60;    // move/swap to a random point in the window
constexpr double kPAdjacent = 0.80;  // swap with the right neighbour in the row
// remaining: shift inside the surrounding gap
}  // namespace sa_tuning

struct SaOptions {
    double hardCap = sa_tuning::kHardCapSec;
    long long iters = 0;          // > 0: fixed move budget, schedule ignores the clock
    std::uint64_t seed = 1;
    std::string logPath;          // CSV log, empty = off
    long long logEvery = 0;       // 0 = automatic
    double initAccept = sa_tuning::kInitAcceptProb;           // experiment override
    double movesPerCell = sa_tuning::kMovesPerCellPerStep;    // experiment override
    double initWindowRows = sa_tuning::kInitWindowRows;       // experiment override
    double lowAlpha = sa_tuning::kLowAcceptAlpha;             // experiment override
    int targetSteps = sa_tuning::kTargetSteps;                // experiment override
    bool verbose = false;
};

struct SaResult {
    double startHpwl = 0, bestHpwl = 0;
    long long moves = 0, accepted = 0;
    int steps = 0;
    std::string stopReason;
};

// Stage 2: anneals from the legal placement `legal` and returns the best
// placement seen in `best` (always legal).
void anneal(const Design& d, const Layout& L, const Placement& legal, const SaOptions& opt,
            Placement& best, SaResult& res);

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "Annealer.h"
#include "Design.h"
#include "Layout.h"
#include "Legalizer.h"
#include "Metrics.h"
#include "Output.h"
#include "Timer.h"

namespace {

// Stage 1's optional improvement passes stop at this time (seconds since
// program start); legalization itself always runs to completion.
constexpr double kStage1Deadline = 15.0;

struct Options {
    std::string in, out;
    bool verbose = false;
    bool l1 = true;
    bool postPass = true;
    bool sa = true;
    SaOptions saOpt;
};

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " <input_base> <output_base> [-v]\n"
              << "  reads <input_base>.aux/.nodes/.nets/.pl/.scl/.wts and writes\n"
              << "  <output_base>_legal.pl, <output_base>.pl and copies of the rest\n"
              << "options:\n"
              << "  -v, --verbose   progress and timing on stderr\n"
              << "  --no-l1         skip the per-segment L1 reposition (Stage 1)\n"
              << "  --no-postpass   skip the displacement post-pass (Stage 1)\n"
              << "  --no-sa         skip Stage 2 (output the legal placement)\n"
              << "  --seed N        SA random seed (default 1)\n"
              << "  --iters N       fixed SA move budget; schedule ignores the clock\n"
              << "  --sa-cap S      SA stops S seconds after program start (default 45)\n"
              << "  --sa-log FILE   write SA progress CSV (also: env DP_SA_LOG)\n"
              << "  --log-every N   CSV row every N attempted moves\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    if (argc < 3) return false;
    if (const char* env = std::getenv("DP_SA_LOG")) o.saOpt.logPath = env;
    o.in = argv[1];
    o.out = argv[2];
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") o.verbose = true;
        else if (a == "--no-l1") o.l1 = false;
        else if (a == "--no-postpass") o.postPass = false;
        else if (a == "--no-sa") o.sa = false;
        else if (i + 1 < argc && a == "--seed") o.saOpt.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (i + 1 < argc && a == "--iters") o.saOpt.iters = std::atoll(argv[++i]);
        else if (i + 1 < argc && a == "--sa-cap") o.saOpt.hardCap = std::atof(argv[++i]);
        else if (i + 1 < argc && a == "--sa-log") o.saOpt.logPath = argv[++i];
        else if (i + 1 < argc && a == "--log-every") o.saOpt.logEvery = std::atoll(argv[++i]);
        else { std::cerr << "error: unknown option " << a << "\n"; return false; }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Timer::start();
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        std::cerr << "error: expected an input and an output base name\n";
        usage(argv[0]);
        return 1;
    }

    Design d;
    std::string err;
    if (!parseDesign(opt.in, d, err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    if (opt.verbose)
        std::fprintf(stderr, "[%.2fs] parsed %zu objects (%zu movable), %zu nets, %zu pins, %zu rows\n",
                     Timer::elapsed(), d.cells.size(), d.movable.size(), d.nets.size(), d.numPins,
                     d.rows.size());

    Layout layout;
    buildLayout(d, layout);
    if (opt.verbose)
        std::fprintf(stderr, "[%.2fs] %zu free row segments\n", Timer::elapsed(), layout.segs.size());

    // ---- Stage 1: legalization
    LegalizerOptions lo;
    lo.l1Reposition = opt.l1;
    lo.postPass = opt.postPass;
    lo.deadline = kStage1Deadline;
    lo.verbose = opt.verbose;
    Placement legal;
    LegalizerTrace trace;
    if (!legalize(d, layout, lo, legal, trace, err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    if (opt.verbose) {
        for (const auto& t : trace)
            std::fprintf(stderr, "  %-9s D_total %.1f  D_max %.1f\n", t.first.c_str(), t.second.total,
                         t.second.max);
        std::fprintf(stderr, "[%.2fs] stage 1 done\n", Timer::elapsed());
    }
    std::string msg;
    if (long long bad = verifyLegal(d, layout, legal, msg)) {
        std::cerr << "internal error: Stage 1 result has " << bad << " violations\n" << msg;
        return 1;
    }

    // ---- Stage 2: simulated annealing
    Placement best = legal;
    if (opt.sa) {
        opt.saOpt.verbose = opt.verbose;
        SaResult sr;
        Placement sa;
        anneal(d, layout, legal, opt.saOpt, sa, sr);
        msg.clear();
        if (long long bad = verifyLegal(d, layout, sa, msg)) {
            std::cerr << "internal error: SA result has " << bad << " violations, keeping Stage 1\n" << msg;
        } else if (totalHpwl(d, sa) <= totalHpwl(d, legal)) {
            best = std::move(sa);
        }
        if (opt.verbose)
            std::fprintf(stderr, "[%.2fs] stage 2 done: %lld moves, %lld accepted, %d steps, stop: %s\n",
                         Timer::elapsed(), sr.moves, sr.accepted, sr.steps, sr.stopReason.c_str());
    }

    if (!writePl(d, legal, opt.out + "_legal.pl") || !writePl(d, best, opt.out + ".pl") ||
        !writeAuxAndCopies(d, opt.out, err)) {
        std::cerr << "error: " << (err.empty() ? "cannot write output .pl" : err) << "\n";
        return 1;
    }

    Displacement disp = displacement(d, legal);
    std::printf("Total displacement (legal): %s\n", formatNumber(disp.total).c_str());
    std::printf("Maximum displacement (legal): %s\n", formatNumber(disp.max).c_str());
    std::printf("Total HPWL (legal): %s\n", formatNumber(totalHpwl(d, legal)).c_str());
    std::printf("Total HPWL (SA): %s\n", formatNumber(totalHpwl(d, best)).c_str());
    if (opt.verbose) std::fprintf(stderr, "[%.2fs] done\n", Timer::elapsed());
    return 0;
}

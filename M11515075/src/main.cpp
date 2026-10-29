#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Design.h"
#include "Metrics.h"
#include "Output.h"
#include "Timer.h"

namespace {

struct Options {
    std::string in, out;
    bool verbose = false;
};

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " <input_base> <output_base> [-v]\n"
              << "  reads <input_base>.aux/.nodes/.nets/.pl/.scl/.wts and writes\n"
              << "  <output_base>_legal.pl, <output_base>.pl and copies of the rest\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    if (argc < 3) return false;
    o.in = argv[1];
    o.out = argv[2];
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") o.verbose = true;
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

    Placement gp;
    gp.x.resize(d.cells.size());
    gp.y.resize(d.cells.size());
    for (size_t i = 0; i < d.cells.size(); ++i) {
        gp.x[i] = d.cells[i].gx;
        gp.y[i] = d.cells[i].gy;
    }

    // Stage 1 and Stage 2 are not implemented yet: pass the input through.
    Placement legal = gp;
    Placement best = legal;

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

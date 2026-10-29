#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// One placeable object from .nodes (standard cell or fixed terminal).
struct Cell {
    std::string name;
    double w = 0, h = 0;
    double gx = 0, gy = 0;   // input (global) placement, lower-left
    bool fixed = false;      // "terminal"/"terminal_NI" in .nodes, or /FIXED in .pl
    bool ni = false;         // terminal_NI or /FIXED_NI: does not block rows
    bool inPl = false;       // appeared in the input .pl
    std::string orient;      // verbatim orientation token, empty if absent
    std::string fixedTag;    // verbatim "/FIXED" style tag, empty if absent
    std::string xStr, yStr;  // verbatim coordinate text from the input .pl
};

struct Net {
    std::string name;
    std::vector<int> pins;   // distinct cell ids; pin offsets are ignored
};

struct Subrow {
    double origin = 0;
    long long numSites = 0;
};

struct Row {
    double y = 0, h = 0;
    double siteW = 1, siteSp = 1;
    std::vector<Subrow> subrows;
};

struct Design {
    // Input file names as resolved from the .aux (paths usable for opening).
    std::string auxPath;
    std::string auxKeyword = "RowBasedPlacement";
    std::vector<std::string> auxFiles;   // file names in .aux order
    std::string nodesFile, netsFile, wtsFile, plFile, sclFile;

    std::vector<Cell> cells;
    std::unordered_map<std::string, int> cellIdx;
    std::vector<Net> nets;
    std::vector<Row> rows;               // sorted by y
    std::vector<int> plOrder;            // output order of objects
    std::vector<int> movable;            // ids of movable cells

    size_t numPins = 0;
};

// Positions of all cells (lower-left), indexed by cell id.
struct Placement {
    std::vector<double> x, y;
};

// Reads <base>.aux and every file it references. Returns false and fills err
// on a fatal problem.
bool parseDesign(const std::string& base, Design& d, std::string& err);

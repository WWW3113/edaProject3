#pragma once

#include <string>

#include "Design.h"

// Up to 3 decimals, trailing zeros (and a bare '.') stripped: 12.500 -> 12.5,
// 757296.000 -> 757296.
std::string formatNumber(double v);

// Writes a Bookshelf .pl with movable cells at p and fixed objects verbatim.
bool writePl(const Design& d, const Placement& p, const std::string& path);

// Writes <out>.aux pointing at <out>.* and copies .nodes/.nets/.scl/.wts.
bool writeAuxAndCopies(const Design& d, const std::string& outBase, std::string& err);

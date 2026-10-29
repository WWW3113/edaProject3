#pragma once

#include "Design.h"

struct Displacement {
    double total = 0, max = 0;
};

// Manhattan displacement of movable cells from their input positions.
Displacement displacement(const Design& d, const Placement& p);

// Unweighted sum over nets of the bounding box of cell centers.
double totalHpwl(const Design& d, const Placement& p);

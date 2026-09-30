#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

struct SolverResult {
    // Identification
    std::string algorithm;

    // Solution
    std::vector<int> tour;
    double cost = std::numeric_limits<double>::infinity();

    // Performance
    double time_ms = 0.0;

    // Reference solution
    double reference_cost =
        std::numeric_limits<double>::quiet_NaN();

    double gap_percent =
        std::numeric_limits<double>::quiet_NaN();
};
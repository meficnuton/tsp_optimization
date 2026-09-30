#pragma once

#include "TSPSolver.h"

#include <cstddef>
#include <string>

// Configuration for the nearest-neighbor greedy heuristic.
struct GreedyConfig : SolverConfig {
    std::size_t start_vertex = 0;

    GreedyConfig() = default;
    explicit GreedyConfig(std::size_t start) : start_vertex(start) {}
};

class GreedySolver : public TSPSolver {
public:
    SolverResult solve(
        const TSPProblem& problem,
        const SolverConfig& config
    ) override;

    std::string name() const override;
};

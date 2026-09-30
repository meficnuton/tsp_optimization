#pragma once

#include "TSPProblem.h"
#include "SolverResult.h"
#include <string>
#include <stdexcept>

// 1. Virtual Config Base Class
class SolverConfig {
public:
    virtual ~SolverConfig() = default;
};

// 2. Update the Interface to accept the config
class TSPSolver {
public:
    virtual ~TSPSolver() = default;

    virtual SolverResult solve(
        const TSPProblem& problem, 
        const SolverConfig& config
    ) = 0;

    virtual std::string name() const = 0;
};
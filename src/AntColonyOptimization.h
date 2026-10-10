#pragma once

#include "TSPSolver.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Configuration for the ant-colony-optimization TSP heuristic (classic Ant
// System).
//
// A seed of zero uses a non-deterministic seed.  Set seed to a non-zero value
// when reproducible results are desired.
struct AntColonyConfig : SolverConfig {
    std::size_t ant_count = 50;        // ants per iteration
    std::size_t iterations = 200;      // compute budget knob
    double alpha = 1.0;                // pheromone exponent (tau^alpha)
    double beta = 5.0;                 // heuristic exponent (eta^beta, eta = 1/d)
    double rho = 0.5;                  // evaporation rate in [0,1]
    double q = 100.0;                  // total pheromone deposited per ant (q / L)
    std::uint64_t seed = 0;

    AntColonyConfig() = default;
    AntColonyConfig(
        std::size_t ants,
        std::size_t iteration_count,
        double pheromone_exponent = 1.0,
        double heuristic_exponent = 5.0,
        double evaporation = 0.5,
        double deposit = 100.0,
        std::uint64_t random_seed = 0
    )
        : ant_count(ants),
          iterations(iteration_count),
          alpha(pheromone_exponent),
          beta(heuristic_exponent),
          rho(evaporation),
          q(deposit),
          seed(random_seed)
    {}
};

// Short name retained for callers that use the same convention as
// GeneticConfig.
using AcoConfig = AntColonyConfig;

class AntColonyOptimization : public TSPSolver {
public:
    SolverResult solve(
        const TSPProblem& problem,
        const SolverConfig& config
    ) override;

    std::string name() const override;
};

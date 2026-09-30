#pragma once

#include "TSPSolver.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Permutation-preserving crossover operators supported by the genetic solver.
enum class GeneticCrossoverType {
    OX,
    PMX
};

// Configuration for the genetic-algorithm TSP heuristic.
//
// A seed of zero uses a non-deterministic seed.  Set seed to a non-zero value
// when reproducible results are desired.
struct GeneticAlgorithmConfig : SolverConfig {
    std::size_t population_size = 100;
    std::size_t generations = 200;
    double mutation_rate = 0.10;
    double crossover_rate = 0.90;
    std::size_t elitism_count = 1;
    std::size_t tournament_size = 3;
    std::uint64_t seed = 0;
    GeneticCrossoverType crossover_type = GeneticCrossoverType::OX;

    GeneticAlgorithmConfig() = default;
    GeneticAlgorithmConfig(
        std::size_t population,
        std::size_t generation_count,
        double mutation = 0.10,
        double crossover = 0.90,
        std::size_t elitism = 1,
        std::size_t tournament = 3,
        std::uint64_t random_seed = 0,
        GeneticCrossoverType crossover_operator = GeneticCrossoverType::OX
    )
        : population_size(population),
          generations(generation_count),
          mutation_rate(mutation),
          crossover_rate(crossover),
          elitism_count(elitism),
          tournament_size(tournament),
          seed(random_seed),
          crossover_type(crossover_operator)
    {}
};

// Short name retained for callers that use the same convention as
// GreedyConfig.
using GeneticConfig = GeneticAlgorithmConfig;

class GeneticAlgorithm : public TSPSolver {
public:
    SolverResult solve(
        const TSPProblem& problem,
        const SolverConfig& config
    ) override;

    std::string name() const override;
};

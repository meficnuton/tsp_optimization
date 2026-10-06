#pragma once

// GeneticAlgorithm2.h
//
// Extended genetic-algorithm TSP solver. Builds on GeneticAlgorithm with:
//
//   * More crossover operators: OX, PMX, CX (cycle), ERX (edge recombination).
//   * More mutation operators: swap, inversion, insertion, scramble, random
//     (one of the four chosen per mutation event).
//   * More selection schemes: tournament, round-robin tournament, roulette
//     (fitness = 1/cost), rank (linear rank).
//   * Early stopping: stop after `early_stop_generations` generations without
//     a strict improvement of the best tour.
//   * Best-ever tracking: the solver returns the best tour seen in ANY
//     generation, not merely the final population's front.
//   * Optional 2-opt after mutation: `two_opt_frequency` = 0 disables it;
//     1 applies 2-opt to every mutated child; k applies it with probability
//     1/k (keep k >= 20 on large instances - 2-opt is O(n^2) per pass).
//   * Seeded initialization: `init_mode` = random (default, same as
//     GeneticAlgorithm), greedy (greedy + 2-opt(greedy) + randoms), or
//     rgreedy (randomized-greedy members replace half the random population).

#include "TSPSolver.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class GeneticCrossoverType2 { OX, PMX, CX, ERX };
enum class GeneticMutationType { SWAP, INVERSION, INSERTION, SCRAMBLE, RANDOM };
enum class GeneticSelectionType { TOURNAMENT, ROUNDROBIN, ROULETTE, RANK };
enum class GeneticInitMode { RANDOM, GREEDY, RGREEDY };

struct GeneticAlgorithmConfig2 : SolverConfig {
    std::size_t population_size = 100;
    std::size_t generations = 200;
    double mutation_rate = 0.10;
    double crossover_rate = 0.90;
    std::size_t elitism_count = 2;
    std::size_t tournament_size = 3;      // tournament size / round-robin pressure
    std::uint64_t seed = 0;               // 0 = non-deterministic
    GeneticCrossoverType2 crossover_type = GeneticCrossoverType2::OX;
    GeneticMutationType mutation_type = GeneticMutationType::SWAP;
    GeneticSelectionType selection_type = GeneticSelectionType::TOURNAMENT;
    std::size_t early_stop_generations = 0; // 0 = disabled
    GeneticInitMode init_mode = GeneticInitMode::RANDOM;
    std::size_t two_opt_frequency = 0;      // 0 = off; 1 = always; k = prob 1/k

    // Optional per-generation best-cost history. When non-null, the solver
    // appends the best-so-far cost once every `history_interval` generations
    // (and always the final generation). The pointer is non-owning; default
    // null disables recording entirely.
    std::vector<double>* best_history = nullptr;
    std::size_t history_interval = 1;

    GeneticAlgorithmConfig2() = default;
};

class GeneticAlgorithm2 : public TSPSolver {
public:
    SolverResult solve(
        const TSPProblem& problem,
        const SolverConfig& config
    ) override;

    std::string name() const override;
};

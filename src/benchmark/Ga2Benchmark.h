#pragma once

// Ga2Benchmark.h
//
// Benchmark support for the extended GeneticAlgorithm2 solver. Reuses
// ReferenceCosts, RunRecord, BenchmarkStats, BenchmarkAggregator and
// ReportWriter from GaBenchmark.h and adds:
//
//   * GaVariant2     - one named GA2 config, including operator types
//                      (selection / crossover / mutation), early stopping,
//                      init mode and 2-opt frequency.
//   * Ga2ConfigFile  - CSV loader. Columns (13, trailing fields optional):
//
//     label,population,generations,mutation_rate,crossover_rate,elitism,
//     tournament,selection,crossover,mutation,early_stop,init,two_opt
//
//     selection: tournament | roundrobin | roulette | rank
//     crossover: OX | PMX | CX | ERX
//     mutation:  swap | inversion | insertion | scramble | random
//     early_stop: 0 = off, else patience in generations
//     init: random | greedy | rgreedy
//     two_opt: 0 = off, 1 = every mutated child, k = probability 1/k
//
//     population and generations are REQUIRED (>= 1); the compute budget is
//     part of the config row. Duplicate labels are rejected. '#' comments and
//     blank lines are skipped; a leading `label,...` header is ignored.

#include "GaBenchmark.h"
#include "../GeneticAlgorithm2.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GaVariant2 {
    std::string label;
    std::size_t population = 150;
    std::size_t generations = 400;
    double mutation_rate = 0.10;
    double crossover_rate = 0.90;
    std::size_t elitism_count = 2;
    std::size_t tournament_size = 3;
    GeneticSelectionType selection_type = GeneticSelectionType::TOURNAMENT;
    GeneticCrossoverType2 crossover_type = GeneticCrossoverType2::OX;
    GeneticMutationType mutation_type = GeneticMutationType::SWAP;
    std::size_t early_stop_generations = 0;
    GeneticInitMode init_mode = GeneticInitMode::RANDOM;
    std::size_t two_opt_frequency = 0;

    // Materializes a concrete config for a given seed.
    GeneticAlgorithmConfig2 toConfig(std::uint64_t seed) const;
};

class Ga2ConfigFile {
public:
    static std::vector<GaVariant2> load(const std::string& path);
};

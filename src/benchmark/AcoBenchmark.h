#pragma once

// AcoBenchmark.h
//
// ACO-specific building blocks for the ACO-vs-Greedy configuration
// benchmark.  The shared infrastructure - ReferenceCosts, RunRecord,
// BenchmarkStats, BenchmarkAggregator, ReportWriter - lives in
// GaBenchmark.h and is reused as-is.
//
//   * AcoVariant    - one named ACO configuration; ants and iterations are
//                     set per config row.
//   * AcoConfigFile - loads benchmark configs from a hand-written CSV file
//                     (one row per config) - no hardcoded sweep.

#include "GaBenchmark.h"
#include "../AntColonyOptimization.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// A named ACO configuration. Ants and iterations are part of the config
// itself (one row in the config CSV); the loader requires both >= 1.
// ---------------------------------------------------------------------------
struct AcoVariant {
    std::string label;
    std::size_t ants = 50;
    std::size_t iterations = 200;
    double alpha = 1.0;   // pheromone exponent
    double beta = 5.0;    // heuristic exponent
    double rho = 0.5;     // evaporation rate
    double q = 100.0;     // deposit per ant

    // Materializes a concrete config for a given seed.
    AntColonyConfig toConfig(std::uint64_t seed) const;
};

// ---------------------------------------------------------------------------
// Benchmark configs from a hand-written CSV file, one row per config:
//
//   label,ants,iterations[,alpha,beta,rho,q]
//
// * '#' comments and blank lines are skipped; a `label,...` header is ignored.
// * ants and iterations are REQUIRED (>= 1): the compute budget is part of
//   the config itself, so a config file is fully self-contained.
// * Any omitted/empty trailing field falls back to the AcoVariant default.
// * rho must be in [0,1]; alpha, beta, q are positive numbers.
// * Duplicate labels are rejected (they would silently merge in the
//   per-config aggregation).
// Throws std::runtime_error (with file:line) on malformed rows.
// ---------------------------------------------------------------------------
class AcoConfigFile {
public:
    static std::vector<AcoVariant> load(const std::string& path);
};

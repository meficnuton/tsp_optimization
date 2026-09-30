#pragma once

// GaBenchmark.h
//
// Reusable, object-oriented building blocks for the GA-vs-Greedy
// configuration benchmark:
//
//   * ReferenceCosts     - loads known-optimal costs from a `name : value`
//                          solutions file (e.g. data/solutions).
//   * GaVariant          - one named GA configuration; population and
//                          generations are set per config row.
//   * GaConfigFile       - loads benchmark configs from a hand-written CSV
//                          file (one row per config) - no hardcoded sweep.
//   * RunRecord          - a single (config, instance, seed) run.
//   * BenchmarkStats      - Welford streaming statistics for gap% and time.
//   * BenchmarkAggregator- collects records, produces per-class/config
//                          rankings and per-instance Greedy-vs-GA results.
//   * ReportWriter       - timestamped CSV (optionally with tours) and a
//                          human-readable summary.

#include "../GeneticAlgorithm.h"
#include "../GreedySolver.h"
#include "../TSPProblem.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Known-optimal tour costs, loaded from a solutions file (not hardcoded).
// File format: `name : value` per line, comments/blank lines ignored.
// ---------------------------------------------------------------------------
class ReferenceCosts {
public:
    // Loads reference costs; throws std::runtime_error if the file is missing.
    explicit ReferenceCosts(const std::string& path);

    // Returns the reference cost, or NaN when the instance is unknown.
    double forInstance(const std::string& name) const;

    std::size_t size() const noexcept { return costs_.size(); }

private:
    std::map<std::string, double> costs_;
};

// ---------------------------------------------------------------------------
// Gap of a cost to its reference optimum, in percent. NaN when the reference
// is unknown or non-positive.
// ---------------------------------------------------------------------------
inline double gapPercentOf(double cost, double reference) {
    if (!(reference > 0.0)) return std::nan("");
    return (cost - reference) / reference * 100.0;
}

// ---------------------------------------------------------------------------
// A named GA configuration. Population and generations are part of the
// config itself (one row in the config CSV); the loader requires both >= 1.
// ---------------------------------------------------------------------------
struct GaVariant {
    std::string label;
    std::size_t population = 150;
    std::size_t generations = 400;
    double mutation_rate = 0.10;
    double crossover_rate = 0.90;
    std::size_t elitism_count = 2;
    std::size_t tournament_size = 3;
    GeneticCrossoverType crossover_type = GeneticCrossoverType::OX;

    // Materializes a concrete config for a given seed.
    GeneticAlgorithmConfig toConfig(std::uint64_t seed) const;
};

// ---------------------------------------------------------------------------
// Benchmark configs from a hand-written CSV file, one row per config:
//
//   label,population,generations[,mutation_rate,crossover_rate,
//                               elitism,tournament,crossover]
//
// * '#' comments and blank lines are skipped; a `label,...` header is ignored.
// * population and generations are REQUIRED (>= 1): the compute budget is
//   part of the config itself, so a config file is fully self-contained.
// * Any omitted/empty trailing field falls back to the GaVariant default.
// * crossover is OX or PMX (case-insensitive).
// * Duplicate labels are rejected (they would silently merge in the
//   per-config aggregation).
// Throws std::runtime_error (with file:line) on malformed rows.
// ---------------------------------------------------------------------------
class GaConfigFile {
public:
    static std::vector<GaVariant> load(const std::string& path);
};

// ---------------------------------------------------------------------------
// A single benchmark run.
// ---------------------------------------------------------------------------
struct RunRecord {
    std::string size_class;
    std::string instance;
    std::size_t dimension = 0;
    std::string config;
    std::uint64_t seed = 0;
    double cost = 0.0;
    double reference_cost = 0.0;
    double gap_percent = 0.0;
    double time_ms = 0.0;
    std::vector<int> tour; // filled only when tours are requested
};

// Streaming mean/std (Welford) plus best value and instance wins.
class BenchmarkStats {
public:
    void add(double gap, double time_ms);
    std::size_t count() const noexcept { return n_; }
    double bestGap() const noexcept { return best_gap_; }
    double meanGap() const noexcept { return mean_gap_; }
    double stdGap() const noexcept;
    double meanTimeMs() const noexcept { return n_ ? total_time_ms_ / n_ : 0.0; }
    void addWin() noexcept { ++wins_; }
    std::size_t wins() const noexcept { return wins_; }

private:
    std::size_t n_ = 0;
    double best_gap_ = 1e300;
    double mean_gap_ = 0.0;
    double m2_gap_ = 0.0;
    double total_time_ms_ = 0.0;
    std::size_t wins_ = 0;
};

// Per-instance aggregate used by the Greedy-vs-GA comparison.
struct InstanceResult {
    std::string instance;
    std::size_t dimension = 0;
    std::string greedy_config;         // e.g. "Greedy(start=0)"
    double greedy_mean_gap = 0.0;
    std::string best_ga_config;        // lowest mean gap among GA variants
    double best_ga_mean_gap = 0.0;
    double baseline_ga_mean_gap = 0.0;
    bool ga_wins = false;              // best GA config beats greedy by mean gap
};

// Collects run records and aggregates them.
class BenchmarkAggregator {
public:
    void add(const RunRecord& record);

    // class -> config -> stats
    const std::map<std::string, std::map<std::string, BenchmarkStats>>&
    byClassAndConfig() const noexcept { return by_class_config_; }

    // Computes the per-instance Greedy-vs-GA comparison tables from the
    // collected records. out is class -> instance -> result.
    static void finalizeInstanceComparisons(
        const std::vector<RunRecord>& records,
        std::map<std::string, std::map<std::string, InstanceResult>>& out,
        std::size_t& ga_wins,
        std::size_t& instance_count
    );

    // Returns run records in insertion order.
    const std::vector<RunRecord>& records() const noexcept { return records_; }

private:
    std::vector<RunRecord> records_;
    std::map<std::string, std::map<std::string, BenchmarkStats>> by_class_config_;
};

// ---------------------------------------------------------------------------
// Output: timestamped CSV files (so repeated runs never collide) and a
// summary table on an std::ostream.
// ---------------------------------------------------------------------------
class ReportWriter {
public:
    // Writes the per-run CSV; returns the file path actually used.
    // When include_tours is true, a `tour` column (semicolon-separated city
    // indices) is appended to every row.
    static std::string writeRunCsv(
        const std::vector<RunRecord>& records,
        const std::string& directory,
        bool include_tours
    );

    // Writes the summary text to a timestamped file next to the CSV and also
    // streams it to `out`. Returns the file path actually used.
    static std::string writeSummary(
        const BenchmarkAggregator& agg,
        std::size_t runs,
        std::ostream& out,
        const std::string& directory
    );

    // "YYYYMMDD_HHMMSS" in local time.
    static std::string timestamp();
};

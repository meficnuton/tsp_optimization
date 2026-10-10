// _benchmark_aco_configs.cpp
//
// Benchmark the ant-colony-optimization TSP solver against the greedy
// baseline across ACO configurations read from a hand-written CSV file.
//
// This harness is the ACO twin of _benchmark_ga_configs.cpp: same
// methodology, same shared benchmark infrastructure (ReferenceCosts,
// RunRecord, BenchmarkAggregator, ReportWriter from GaBenchmark), only the
// solver and the config format differ.
//
// Size classes are AUTO-DISCOVERED: every directory under data/ that contains
// at least one .tsp instance is a size class (data/solutions is a file and
// dirs without instances are skipped). Nothing about the test set is
// hardcoded, and all ACO parameters - including the compute budget
// (ants, iterations) - live in the config CSV.
//
// Methodology:
//   * ACO configs are typed by hand in a CSV file, one row per config - a
//     benchmark is just a config file, no recompile needed.
//   * The greedy baseline is deterministic, so it runs ONCE per instance;
//     only the stochastic ACO is repeated with --runs seeds.
//   * Multiple seeds per (config, instance): we report best/mean/std of the
//     gap to the known optimum (loaded from data/solutions) plus mean time.
//   * Per-instance Greedy-vs-ACO comparison with win rates. A config whose
//     label starts with "baseline" fills the baselineACO% column.
//
// Usage (run from the tsp/ repo root):
//   ./bin/_benchmark_aco_configs.exe [options]
// Options:
//   --runs=N            seeds per (config, instance)   [default: 5]
//   --classes=A,B       subset of the auto-discovered data/ dirs
//   --configs=PATH      ACO config CSV file           [default: configs/aco_config_default.csv]
//   --filter=SUBSTR     only run configs whose label contains SUBSTR
//   --tours             append a `tour` column to the per-run CSV
//
// Writes a timestamped per-run CSV plus a timestamped summary .txt under
// results/, and prints the summary to stdout.

#include "benchmark/AcoBenchmark.h"
#include "benchmark/ConfigEcho.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::optional<std::string> optionValue(const std::string& arg,
                                       const std::string& name) {
    const std::string prefix = "--" + name + "=";
    if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
    return std::nullopt;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

// Reproducible seed for (config, instance, run).
std::uint64_t makeSeed(const std::string& config, const std::string& instance,
                       std::size_t run) {
    std::uint64_t seed = 1000003ull;
    for (char c : config) seed = seed * 131ull + static_cast<unsigned char>(c);
    for (char c : instance)
        seed = seed * 131ull + static_cast<unsigned char>(c);
    return seed * 6364136223846793005ull + 1442695040888963407ull + run;
}

} // namespace

int main(int argc, char** argv) {
    std::size_t runs = 5;
    std::set<std::string> class_filter; // empty = all
    std::string config_filter;          // empty = all
    std::string configs_path = "configs/aco_config_default.csv";
    bool print_tours = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (auto v = optionValue(arg, "runs")) runs = std::stoul(*v);
        else if (auto v = optionValue(arg, "classes"))
            for (auto& c : split(*v, ',')) class_filter.insert(c);
        else if (auto v = optionValue(arg, "configs")) configs_path = *v;
        else if (auto v = optionValue(arg, "filter")) config_filter = *v;
        else if (arg == "--tours") print_tours = true;
        else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    if (runs == 0) runs = 1;

    // ---- reference costs from file (no hardcoded optima) -------------------
    ReferenceCosts refs("data/solutions");
    std::cerr << "[load] " << refs.size()
              << " reference costs from data/solutions\n";

    // ---- auto-discover size classes under data/ ----------------------------
    struct SizeClass {
        std::string dir;      // data/<dir>
        std::string label;
    };
    std::vector<SizeClass> classes;
    for (const auto& entry : std::filesystem::directory_iterator("data")) {
        if (!entry.is_directory()) continue;
        bool has_tsp = false;
        for (const auto& e :
             std::filesystem::directory_iterator(entry.path())) {
            if (e.path().extension() == ".tsp") {
                has_tsp = true;
                break;
            }
        }
        if (!has_tsp) continue;
        SizeClass cls;
        cls.dir = entry.path().filename().string();
        cls.label = cls.dir;
        classes.push_back(std::move(cls));
    }
    std::sort(classes.begin(), classes.end(),
              [](const SizeClass& a, const SizeClass& b) { return a.dir < b.dir; });
    if (classes.empty()) {
        std::cerr << "no data/ subdirectory contains .tsp instances\n";
        return 2;
    }

    // Apply --classes filter.
    std::vector<SizeClass> selected;
    for (const auto& cls : classes) {
        if (class_filter.empty() || class_filter.count(cls.label))
            selected.push_back(cls);
    }
    if (selected.empty()) {
        std::cerr << "no size classes selected via --classes (available:";
        for (const auto& cls : classes) std::cerr << ' ' << cls.label;
        std::cerr << ")\n";
        return 2;
    }

    // ---- ACO configs from file (mandatory) ----------------------------------
    std::vector<AcoVariant> variants;
    try {
        variants = AcoConfigFile::load(configs_path);
        std::cerr << "[load] " << variants.size() << " ACO configs from "
                  << configs_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
    if (!config_filter.empty()) {
        std::vector<AcoVariant> keep;
        for (const auto& v : variants)
            if (v.label.find(config_filter) != std::string::npos)
                keep.push_back(v);
        if (keep.empty()) {
            std::cerr << "no configs match --filter=" << config_filter << "\n";
            return 2;
        }
        variants = std::move(keep);
    }

    // ---- load problems per class -------------------------------------------
    struct LoadedClass {
        SizeClass cls;
        std::vector<TSPProblem> problems;
    };
    std::vector<LoadedClass> loaded;
    for (const auto& cls : selected) {
        LoadedClass lc{cls, {}};
        std::vector<std::filesystem::path> files;
        for (const auto& entry :
             std::filesystem::directory_iterator("data/" + cls.dir)) {
            if (entry.path().extension() == ".tsp") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files)
            lc.problems.push_back(TSPProblem::fromTspFile(f.string()));
        std::cerr << "[load] " << cls.label << ": " << lc.problems.size()
                  << " instances\n";
        loaded.push_back(std::move(lc));
    }

    AntColonyOptimization aco;
    GreedySolver greedy;
    BenchmarkAggregator aggregator;

    // ---- run everything ------------------------------------------------------
    for (const auto& lc : loaded) {
        // Greedy baseline: deterministic, so it is solved ONCE per instance.
        for (const auto& problem : lc.problems) {
            GreedyConfig greedy_cfg(0);
            const SolverResult r = greedy.solve(problem, greedy_cfg);

            RunRecord rec;
            rec.size_class = lc.cls.label;
            rec.instance = problem.name();
            rec.dimension = problem.dimension();
            rec.config = "Greedy(start=0)";
            rec.seed = 0;
            rec.cost = r.cost;
            rec.reference_cost = refs.forInstance(problem.name());
            rec.gap_percent = gapPercentOf(r.cost, rec.reference_cost);
            rec.time_ms = r.time_ms;
            if (print_tours) rec.tour = r.tour;
            aggregator.add(rec);
            std::cerr << "\r[" << lc.cls.label << "] Greedy(start=0) / "
                      << problem.name() << "        " << std::flush;
        }
        std::cerr << "\n";

        for (const auto& variant : variants) {
            for (const auto& problem : lc.problems) {
                for (std::size_t run = 0; run < runs; ++run) {
                    const std::uint64_t seed =
                        makeSeed(variant.label, problem.name(), run);
                    const AntColonyConfig cfg = variant.toConfig(seed);
                    const SolverResult r = aco.solve(problem, cfg);

                    RunRecord rec;
                    rec.size_class = lc.cls.label;
                    rec.instance = problem.name();
                    rec.dimension = problem.dimension();
                    rec.config = variant.label;
                    rec.seed = seed;
                    rec.cost = r.cost;
                    rec.reference_cost = refs.forInstance(problem.name());
                    rec.gap_percent = gapPercentOf(r.cost, rec.reference_cost);
                    rec.time_ms = r.time_ms;
                    if (print_tours) rec.tour = r.tour;
                    aggregator.add(rec);

                    std::cerr << "\r[" << lc.cls.label << "] " << variant.label
                              << " / " << problem.name() << " run " << (run + 1)
                              << "/" << runs << "        " << std::flush;
                }
            }
            std::cerr << "\n";
        }
    }

    // ---- write outputs --------------------------------------------------------
    std::filesystem::create_directories("results");
    const std::string csv_path =
        ReportWriter::writeRunCsv(aggregator.records(), "results", print_tours);
    const std::string summary_path =
        ReportWriter::writeSummary(aggregator, runs, std::cout, "results");

    std::cerr << "\nper-run CSV:  " << csv_path << "\n";
    std::cerr << "summary file: " << summary_path << "\n";

    // Echo the config file into the summary so readers of the summary alone
    // know exactly which parameter sets were tested.
    std::vector<std::pair<std::string, std::string>> config_rows;
    for (const auto& variant : variants) {
        std::ostringstream desc;
        desc << "ants=" << variant.ants << " iters=" << variant.iterations
             << " alpha=" << variant.alpha << " beta=" << variant.beta
             << " rho=" << variant.rho << " q=" << variant.q;
        config_rows.emplace_back(variant.label, desc.str());
    }
    writeConfigEcho(std::cout, summary_path, config_rows);
    return 0;
}

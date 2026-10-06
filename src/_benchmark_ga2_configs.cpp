// _benchmark_ga2_configs.cpp
//
// Benchmark the extended GeneticAlgorithm2 solver (operator variants:
// selection / crossover / mutation, early stopping, seeded initialization,
// optional 2-opt) against the greedy baseline. Configuration is read from a
// hand-written CSV - a benchmark is a config file, no recompile needed.
//
// Same methodology and CLI as _benchmark_ga_configs.cpp; the config CSV has
// extended columns (see benchmark/Ga2Benchmark.h):
//
//   label,population,generations,mutation_rate,crossover_rate,elitism,
//   tournament,selection,crossover,mutation,early_stop,init,two_opt
//
// Usage (run from the tsp/ repo root):
//   ./bin/_benchmark_ga2_configs.exe [options]
// Options:
//   --runs=N            seeds per (config, instance)   [default: 5]
//   --classes=A,B       subset of the auto-discovered data/ dirs
//   --configs=PATH      GA2 config CSV file           [default: configs/ga2_configs.csv]
//   --filter=SUBSTR     only run configs whose label contains SUBSTR
//   --tours             append a `tour` column to the per-run CSV

#include "benchmark/Ga2Benchmark.h"
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
    std::string configs_path = "configs/ga2_configs.csv";
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
        std::string dir;
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

    // ---- GA2 configs from file (mandatory) ----------------------------------
    std::vector<GaVariant2> variants;
    try {
        variants = Ga2ConfigFile::load(configs_path);
        std::cerr << "[load] " << variants.size() << " GA2 configs from "
                  << configs_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
    if (!config_filter.empty()) {
        std::vector<GaVariant2> keep;
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

    GeneticAlgorithm2 ga;
    GreedySolver greedy;
    BenchmarkAggregator aggregator;

    // ---- run everything ------------------------------------------------------
    for (const auto& lc : loaded) {
        // Greedy baseline: deterministic, solved ONCE per instance.
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
                    const GeneticAlgorithmConfig2 cfg = variant.toConfig(seed);
                    const SolverResult r = ga.solve(problem, cfg);

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
    const auto crossover_name = [](GeneticCrossoverType2 t) {
        switch (t) {
        case GeneticCrossoverType2::OX:  return "OX";
        case GeneticCrossoverType2::PMX: return "PMX";
        case GeneticCrossoverType2::CX:  return "CX";
        case GeneticCrossoverType2::ERX: return "ERX";
        }
        return "?";
    };
    const auto mutation_name = [](GeneticMutationType t) {
        switch (t) {
        case GeneticMutationType::SWAP:      return "swap";
        case GeneticMutationType::INVERSION: return "inversion";
        case GeneticMutationType::INSERTION: return "insertion";
        case GeneticMutationType::SCRAMBLE:  return "scramble";
        case GeneticMutationType::RANDOM:    return "random";
        }
        return "?";
    };
    const auto selection_name = [](GeneticSelectionType t) {
        switch (t) {
        case GeneticSelectionType::TOURNAMENT: return "tournament";
        case GeneticSelectionType::ROUNDROBIN: return "roundrobin";
        case GeneticSelectionType::ROULETTE:   return "roulette";
        case GeneticSelectionType::RANK:       return "rank";
        }
        return "?";
    };
    const auto init_name = [](GeneticInitMode t) {
        switch (t) {
        case GeneticInitMode::RANDOM:  return "random";
        case GeneticInitMode::GREEDY:  return "greedy";
        case GeneticInitMode::RGREEDY: return "rgreedy";
        }
        return "?";
    };

    std::vector<std::pair<std::string, std::string>> config_rows;
    for (const auto& variant : variants) {
        std::ostringstream desc;
        desc << "pop=" << variant.population << " gen=" << variant.generations
             << " mut=" << variant.mutation_rate << " cxr=" << variant.crossover_rate
             << " elit=" << variant.elitism_count << " tour=" << variant.tournament_size
             << " selection=" << selection_name(variant.selection_type)
             << " crossover=" << crossover_name(variant.crossover_type)
             << " mutation=" << mutation_name(variant.mutation_type)
             << " early_stop=" << variant.early_stop_generations
             << " init=" << init_name(variant.init_mode)
             << " two_opt=" << variant.two_opt_frequency;
        config_rows.emplace_back(variant.label, desc.str());
    }
    writeConfigEcho(std::cout, summary_path, config_rows);
    return 0;
}

// _benchmark_ga2_convergence.cpp
//
// Records the per-generation convergence of every GA2 config: for each
// (config, instance, seed) run it samples the best-so-far cost at a fixed
// number of generation checkpoints and writes a timestamped CSV
//
//   size_class,instance,config,seed,sample,generation,best_cost,gap_percent
//
// `sample` is the checkpoint index (0 = generation 0 baseline, last = final
// generation); generation is the actual generation number, so configs with
// different budgets stay comparable on a shared x-axis.
//
// Usage (run from the tsp/ repo root):
//   ./bin/_benchmark_ga2_convergence.exe [options]
// Options:
//   --runs=N         seeds per (config, instance)  [default: 3]
//   --classes=A,B    subset of the auto-discovered data/ dirs
//   --configs=PATH   GA2 config CSV                [default: configs/ga2_configs.csv]
//   --filter=SUBSTR  only run configs whose label contains SUBSTR
//   --samples=N      checkpoints per run           [default: 40]
//
// Visualize with:  python tools/plot_convergence.py --csv <this file>

#include "benchmark/Ga2Benchmark.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
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

std::uint64_t makeSeed(const std::string& config, const std::string& instance,
                       std::size_t run) {
    std::uint64_t seed = 1000003ull;
    for (char c : config) seed = seed * 131ull + static_cast<unsigned char>(c);
    for (char c : instance)
        seed = seed * 131ull + static_cast<unsigned char>(c);
    return seed * 6364136223846793005ull + 1442695040888963407ull + run;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ss.str();
}

} // namespace

int main(int argc, char** argv) {
    std::size_t runs = 3;
    std::size_t samples = 40;
    std::set<std::string> class_filter;
    std::string config_filter;
    std::string configs_path = "configs/ga2_configs.csv";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (auto v = optionValue(arg, "runs")) runs = std::stoul(*v);
        else if (auto v = optionValue(arg, "samples")) samples = std::stoul(*v);
        else if (auto v = optionValue(arg, "classes"))
            for (auto& c : split(*v, ',')) class_filter.insert(c);
        else if (auto v = optionValue(arg, "configs")) configs_path = *v;
        else if (auto v = optionValue(arg, "filter")) config_filter = *v;
        else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    if (runs == 0) runs = 1;
    if (samples < 2) samples = 2;

    ReferenceCosts refs("data/solutions");

    // ---- auto-discover size classes -----------------------------------------
    struct SizeClass { std::string dir, label; };
    std::vector<SizeClass> classes;
    for (const auto& entry : std::filesystem::directory_iterator("data")) {
        if (!entry.is_directory()) continue;
        bool has_tsp = false;
        for (const auto& e : std::filesystem::directory_iterator(entry.path())) {
            if (e.path().extension() == ".tsp") { has_tsp = true; break; }
        }
        if (has_tsp) classes.push_back({entry.path().filename().string(),
                                        entry.path().filename().string()});
    }
    std::sort(classes.begin(), classes.end(),
              [](const SizeClass& a, const SizeClass& b) { return a.dir < b.dir; });

    std::vector<SizeClass> selected;
    for (const auto& cls : classes) {
        if (class_filter.empty() || class_filter.count(cls.label))
            selected.push_back(cls);
    }
    if (selected.empty()) {
        std::cerr << "no size classes selected via --classes\n";
        return 2;
    }

    // ---- configs -------------------------------------------------------------
    std::vector<GaVariant2> variants;
    try {
        variants = Ga2ConfigFile::load(configs_path);
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

    // ---- output CSV ----------------------------------------------------------
    std::filesystem::create_directories("results");
    const std::string out_path =
        "results/ga2_convergence_" + timestamp() + ".csv";
    std::ofstream out(out_path);
    out << "size_class,instance,config,seed,sample,generation,best_cost,gap_percent\n"
        << std::fixed << std::setprecision(4);
    std::cerr << "[out] " << out_path << "\n";

    GeneticAlgorithm2 ga;

    for (const auto& cls : selected) {
        std::vector<std::filesystem::path> files;
        for (const auto& entry :
             std::filesystem::directory_iterator("data/" + cls.dir)) {
            if (entry.path().extension() == ".tsp") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        std::cerr << "[load] " << cls.label << ": " << files.size()
                  << " instances\n";

        for (const auto& f : files) {
            const TSPProblem problem = TSPProblem::fromTspFile(f.string());
            const double reference = refs.forInstance(problem.name());

            for (const auto& variant : variants) {
                const std::size_t interval =
                    std::max<std::size_t>(1, variant.generations / samples);
                for (std::size_t run = 0; run < runs; ++run) {
                    const std::uint64_t seed =
                        makeSeed(variant.label, problem.name(), run);
                    GeneticAlgorithmConfig2 cfg = variant.toConfig(seed);
                    std::vector<double> history;
                    history.reserve(variant.generations / interval + 2);
                    cfg.best_history = &history;
                    cfg.history_interval = interval;
                    ga.solve(problem, cfg);

                    for (std::size_t s = 0; s < history.size(); ++s) {
                        const std::size_t generation =
                            s == 0 ? 0
                            : (s - 1) * interval + interval; // gen 0 baseline then sampled
                        // The final entry may duplicate the last sampled one
                        // when generations % interval == 0 - keep it, it is
                        // the definitive end point.
                        out << cls.label << ',' << problem.name() << ','
                            << variant.label << ',' << seed << ',' << s << ','
                            << generation << ',' << history[s] << ','
                            << gapPercentOf(history[s], reference) << '\n';
                    }
                    std::cerr << "\r[" << cls.label << "] " << problem.name()
                              << " / " << variant.label << " run " << (run + 1)
                              << "/" << runs << "        " << std::flush;
                }
            }
            std::cerr << "\n";
        }
    }
    out.flush();
    std::cerr << "\nconvergence CSV: " << out_path << "\n"
              << "plot with: python tools/plot_convergence.py --csv "
              << out_path << "\n";
    return 0;
}

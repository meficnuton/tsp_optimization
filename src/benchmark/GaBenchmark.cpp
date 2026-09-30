// GaBenchmark.cpp - implementation of the GA-vs-Greedy benchmark building
// blocks declared in GaBenchmark.h.

#include "GaBenchmark.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// ReferenceCosts
// ---------------------------------------------------------------------------
ReferenceCosts::ReferenceCosts(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open solutions file: " + path);
    }
    std::string line;
    while (std::getline(in, line)) {
        // `name : 12345` (possible trailing comment such as "(CEIL_2D)")
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::string rest = line.substr(colon + 1);
        // trim whitespace
        const auto isSpace = [](unsigned char c) { return std::isspace(c); };
        name.erase(name.find_last_not_of(" \t") + 1);
        name.erase(0, name.find_first_not_of(" \t"));
        if (name.empty()) continue;
        try {
            const double value = std::stod(rest);
            costs_[name] = value;
        } catch (const std::exception&) {
            // line without a numeric value - skip
        }
    }
}

double ReferenceCosts::forInstance(const std::string& name) const {
    const auto it = costs_.find(name);
    return it == costs_.end() ? std::nan("") : it->second;
}

// ---------------------------------------------------------------------------
// GaVariant
// ---------------------------------------------------------------------------
GeneticAlgorithmConfig GaVariant::toConfig(std::uint64_t seed) const {
    GeneticAlgorithmConfig cfg;
    cfg.population_size = population;
    cfg.generations = generations;
    cfg.mutation_rate = mutation_rate;
    cfg.crossover_rate = crossover_rate;
    cfg.elitism_count = elitism_count;
    cfg.tournament_size = tournament_size;
    cfg.seed = seed;
    cfg.crossover_type = crossover_type;
    return cfg;
}

// ---------------------------------------------------------------------------
// Shared CSV-parsing helpers for GaConfigFile
// ---------------------------------------------------------------------------
namespace {

std::string trimCopy(const std::string& s) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    std::size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Splits on commas, keeping empty fields (an empty field = "keep the default").
std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

// Strips a trailing comment line-wise helper: returns the part before '#'.
std::string stripComment(const std::string& line) {
    const auto hash = line.find('#');
    return hash == std::string::npos ? line : line.substr(0, hash);
}

[[noreturn]] void fail(const std::string& path, std::size_t line_no,
                       const std::string& msg) {
    throw std::runtime_error(path + ":" + std::to_string(line_no) + ": " + msg);
}

std::size_t parseSizeField(const std::string& tok, const std::string& path,
                           std::size_t line_no, const char* name) {
    if (tok.empty()) return 0; // caller decides what "empty" means
    if (tok.find_first_not_of("0123456789") != std::string::npos)
        fail(path, line_no, std::string(name) + " must be a non-negative integer, got '" + tok + "'");
    try {
        return static_cast<std::size_t>(std::stoull(tok));
    } catch (const std::exception&) {
        fail(path, line_no, std::string(name) + " out of range: '" + tok + "'");
    }
}

double parseDoubleField(const std::string& tok, const std::string& path,
                         std::size_t line_no, const char* name) {
    try {
        std::size_t pos = 0;
        const double v = std::stod(tok, &pos);
        if (pos != tok.size())
            fail(path, line_no, std::string(name) + " has trailing junk: '" + tok + "'");
        return v;
    } catch (const std::out_of_range&) {
        fail(path, line_no, std::string(name) + " out of range: '" + tok + "'");
    } catch (const std::exception&) {
        fail(path, line_no, std::string(name) + " must be a number, got '" + tok + "'");
    }
}

GeneticCrossoverType parseCrossoverField(const std::string& tok,
                                         const std::string& path,
                                         std::size_t line_no) {
    std::string up;
    up.reserve(tok.size());
    for (char c : tok)
        up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (up == "OX") return GeneticCrossoverType::OX;
    if (up == "PMX") return GeneticCrossoverType::PMX;
    fail(path, line_no, "crossover must be OX or PMX, got '" + tok + "'");
}

} // namespace

// ---------------------------------------------------------------------------
// GaConfigFile
// ---------------------------------------------------------------------------
std::vector<GaVariant> GaConfigFile::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open GA config file: " + path);

    std::vector<GaVariant> out;
    std::set<std::string> seen;
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string body = trimCopy(stripComment(line));
        if (body.empty()) continue;

        std::vector<std::string> f = splitCsv(body);
        for (auto& tok : f) tok = trimCopy(tok);

        if (f.empty() || f[0].empty()) fail(path, line_no, "missing label");
        if (f[0] == "label") continue; // header row
        if (f.size() > 8)
            fail(path, line_no, "too many fields (max 8: label,population,"
                                "generations,mutation_rate,crossover_rate,"
                                "elitism,tournament,crossover)");

        GaVariant v; // trailing fields left empty fall back to these defaults
        v.label = f[0];
        if (f.size() > 1 && !f[1].empty()) v.population = parseSizeField(f[1], path, line_no, "population");
        if (f.size() > 2 && !f[2].empty()) v.generations = parseSizeField(f[2], path, line_no, "generations");
        if (v.population == 0 || v.generations == 0)
            fail(path, line_no, "population and generations are required (>= 1);"
                                " the compute budget is part of the config row");
        if (f.size() > 3 && !f[3].empty()) {
            v.mutation_rate = parseDoubleField(f[3], path, line_no, "mutation_rate");
            if (!(v.mutation_rate >= 0.0 && v.mutation_rate <= 1.0))
                fail(path, line_no, "mutation_rate must be in [0,1]");
        }
        if (f.size() > 4 && !f[4].empty()) {
            v.crossover_rate = parseDoubleField(f[4], path, line_no, "crossover_rate");
            if (!(v.crossover_rate >= 0.0 && v.crossover_rate <= 1.0))
                fail(path, line_no, "crossover_rate must be in [0,1]");
        }
        if (f.size() > 5 && !f[5].empty()) v.elitism_count = parseSizeField(f[5], path, line_no, "elitism");
        if (f.size() > 6 && !f[6].empty()) {
            v.tournament_size = parseSizeField(f[6], path, line_no, "tournament");
            if (v.tournament_size == 0)
                fail(path, line_no, "tournament must be >= 1");
        }
        if (f.size() > 7 && !f[7].empty())
            v.crossover_type = parseCrossoverField(f[7], path, line_no);

        if (!seen.insert(v.label).second)
            fail(path, line_no, "duplicate label '" + v.label + "'");
        out.push_back(std::move(v));
    }
    if (out.empty())
        throw std::runtime_error(path + ": no configs found (need at least one row)");
    return out;
}

// ---------------------------------------------------------------------------
// BenchmarkStats
// ---------------------------------------------------------------------------
void BenchmarkStats::add(double gap, double time_ms) {
    ++n_;
    best_gap_ = std::min(best_gap_, gap);
    const double delta = gap - mean_gap_;
    mean_gap_ += delta / static_cast<double>(n_);
    m2_gap_ += delta * (gap - mean_gap_);
    total_time_ms_ += time_ms;
}

double BenchmarkStats::stdGap() const noexcept {
    return n_ > 1 ? std::sqrt(m2_gap_ / static_cast<double>(n_ - 1)) : 0.0;
}

// ---------------------------------------------------------------------------
// BenchmarkAggregator
// ---------------------------------------------------------------------------
void BenchmarkAggregator::add(const RunRecord& record) {
    records_.push_back(record);
    by_class_config_[record.size_class][record.config].add(
        record.gap_percent, record.time_ms);
}

void BenchmarkAggregator::finalizeInstanceComparisons(
    const std::vector<RunRecord>& records,
    std::map<std::string, std::map<std::string, InstanceResult>>& out,
    std::size_t& ga_wins,
    std::size_t& instance_count
) {
    out.clear();
    ga_wins = 0;
    instance_count = 0;

    // class|instance -> config -> (sum gap, count)
    std::map<std::string,
             std::map<std::string, std::pair<double, std::size_t>>> sums;
    std::map<std::string, std::pair<std::size_t, std::string>> dims;

    for (const auto& r : records) {
        sums[r.size_class + "|" + r.instance][r.config].first += r.gap_percent;
        ++sums[r.size_class + "|" + r.instance][r.config].second;
        dims[r.size_class + "|" + r.instance] = {r.dimension, r.instance};
    }

    for (const auto& [key, configs] : sums) {
        const std::size_t sep = key.find('|');
        const std::string cls = key.substr(0, sep);
        const std::string inst = key.substr(sep + 1);

        InstanceResult res;
        res.instance = inst;
        res.dimension = dims[key].first;
        double best_ga = 1e300;
        for (const auto& [cfg, sc] : configs) {
            const double mean_gap = sc.first / static_cast<double>(sc.second);
            if (cfg.rfind("Greedy", 0) == 0) {
                res.greedy_config = cfg;
                res.greedy_mean_gap = mean_gap;
            } else {
                if (mean_gap < best_ga) {
                    best_ga = mean_gap;
                    res.best_ga_config = cfg;
                    res.best_ga_mean_gap = mean_gap;
                }
                if (cfg.rfind("baseline", 0) == 0) {
                    res.baseline_ga_mean_gap = mean_gap;
                }
            }
        }
        res.ga_wins =
            !res.best_ga_config.empty() &&
            res.best_ga_mean_gap < res.greedy_mean_gap;
        if (res.ga_wins) ++ga_wins;
        ++instance_count;
        out[cls][inst] = res;
    }
}

// ---------------------------------------------------------------------------
// ReportWriter
// ---------------------------------------------------------------------------
std::string ReportWriter::timestamp() {
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

std::string ReportWriter::writeRunCsv(
    const std::vector<RunRecord>& records,
    const std::string& directory,
    bool include_tours
) {
    const std::string path =
        directory + "/ga_benchmark_" + timestamp() + ".csv";
    std::ofstream out(path);
    out << "size_class,instance,dimension,config,seed,cost,reference_cost,"
           "gap_percent,time_ms";
    if (include_tours) out << ",tour";
    out << "\n" << std::fixed << std::setprecision(4);
    for (const auto& r : records) {
        out << r.size_class << ',' << r.instance << ',' << r.dimension << ','
            << r.config << ',' << r.seed << ',' << r.cost << ','
            << r.reference_cost << ',' << r.gap_percent << ',' << r.time_ms;
        if (include_tours) {
            out << ',';
            for (std::size_t i = 0; i < r.tour.size(); ++i) {
                if (i) out << ' ';
                out << r.tour[i];
            }
        }
        out << '\n';
    }
    return path;
}

std::string ReportWriter::writeSummary(
    const BenchmarkAggregator& agg,
    std::size_t runs,
    std::ostream& out,
    const std::string& directory
) {
    std::map<std::string, std::map<std::string, InstanceResult>> instances;
    std::size_t ga_wins = 0, instance_count = 0;
    BenchmarkAggregator::finalizeInstanceComparisons(agg.records(), instances,
                                                     ga_wins, instance_count);

    std::ostringstream body;
    body << std::fixed << std::setprecision(2);

    body << "=== GA vs Greedy configuration benchmark ===\n";
    body << "seeds per (config, instance): " << runs << "\n";
    body << "instances compared: " << instance_count
          << "  GA wins: " << ga_wins << " ("
          << (instance_count ? 100.0 * ga_wins / instance_count : 0.0)
          << "%)\n\n";

    for (const auto& [cls, configs] : agg.byClassAndConfig()) {
        body << "--- " << cls << " : config ranking (by mean gap%) ---\n";
        body << "config                        mean_gap%  best_gap%  "
                 "std_gap%  mean_ms  wins\n";
        // count per-config wins across this class's instances
        std::map<std::string, std::size_t> win_counts;
        const auto inst_it = instances.find(cls);
        if (inst_it != instances.end()) {
            for (const auto& [inst, res] : inst_it->second) {
                if (res.ga_wins && !res.best_ga_config.empty()) {
                    ++win_counts[res.best_ga_config];
                }
            }
        }
        std::vector<const std::pair<const std::string, BenchmarkStats>*> rows;
        for (const auto& kv : configs) rows.push_back(&kv);
        std::sort(rows.begin(), rows.end(),
                  [](auto* a, auto* b) {
                      return a->second.meanGap() < b->second.meanGap();
                  });
        for (const auto* row : rows) {
            const BenchmarkStats& s = row->second;
            const auto w = win_counts.find(row->first);
            body << std::left << std::setw(29) << row->first << ' '
                 << std::right << std::setw(8) << s.meanGap() << std::setw(10)
                 << s.bestGap() << std::setw(10) << s.stdGap() << std::setw(9)
                 << s.meanTimeMs() << std::setw(6)
                 << (w == win_counts.end() ? 0 : w->second) << '\n';
        }
        body << '\n';

        // Per-instance Greedy-vs-GA breakdown.
        const auto it = instances.find(cls);
        if (it != instances.end()) {
            std::size_t class_wins = 0;
            for (const auto& [inst, res] : it->second) {
                if (res.ga_wins) ++class_wins;
            }
            body << "--- " << cls << " : per-instance (Greedy vs best GA) ---\n";
            body << "instance      dim    greedy%   bestGA%  baselineGA%  "
                    "winner  best_ga_config\n";
            for (const auto& [inst, res] : it->second) {
                body << std::left << std::setw(13) << inst << ' '
                     << std::right << std::setw(5) << res.dimension
                     << std::setw(9) << res.greedy_mean_gap << std::setw(10)
                     << res.best_ga_mean_gap << std::setw(12)
                     << res.baseline_ga_mean_gap << "  "
                     << std::left << std::setw(7)
                     << (res.ga_wins ? "GA" : "Greedy") << ' '
                     << res.best_ga_config << '\n';
            }
            body << "class GA win rate: " << class_wins << "/"
                 << it->second.size() << "\n\n";
        }
    }

    out << body.str();

    const std::string path =
        directory + "/ga_benchmark_summary_" + timestamp() + ".txt";
    std::ofstream file(path);
    file << body.str();
    return path;
}

// Ga2Benchmark.cpp - GaVariant2 + CSV loader for GeneticAlgorithm2 configs.

#include "Ga2Benchmark.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

std::string trimCopy(const std::string& s) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    std::size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

std::string stripComment(const std::string& line) {
    const auto hash = line.find('#');
    return hash == std::string::npos ? line : line.substr(0, hash);
}

[[noreturn]] void fail(const std::string& path, std::size_t line_no,
                       const std::string& msg) {
    throw std::runtime_error(path + ":" + std::to_string(line_no) + ": " + msg);
}

std::string upperCopy(const std::string& tok) {
    std::string up;
    up.reserve(tok.size());
    for (char c : tok)
        up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return up;
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

template <typename ParseFn>
auto parseEnumField(const std::string& tok, const std::string& path,
                    std::size_t line_no, const char* name, ParseFn parse)
    -> decltype(parse(std::string{}, path, line_no))
{
    if (tok.empty()) fail(path, line_no, std::string(name) + " must not be empty");
    return parse(tok, path, line_no);
}

GeneticSelectionType parseSelection(const std::string& tok,
                                    const std::string& path,
                                    std::size_t line_no) {
    const std::string up = upperCopy(tok);
    if (up == "TOURNAMENT" || up == "TOUR") return GeneticSelectionType::TOURNAMENT;
    if (up == "ROUNDROBIN" || up == "RR")   return GeneticSelectionType::ROUNDROBIN;
    if (up == "ROULETTE")                   return GeneticSelectionType::ROULETTE;
    if (up == "RANK")                       return GeneticSelectionType::RANK;
    fail(path, line_no, "selection must be tournament|roundrobin|roulette|rank, got '" + tok + "'");
}

GeneticCrossoverType2 parseCrossover2(const std::string& tok,
                                      const std::string& path,
                                      std::size_t line_no) {
    const std::string up = upperCopy(tok);
    if (up == "OX")  return GeneticCrossoverType2::OX;
    if (up == "PMX") return GeneticCrossoverType2::PMX;
    if (up == "CX")  return GeneticCrossoverType2::CX;
    if (up == "ERX") return GeneticCrossoverType2::ERX;
    fail(path, line_no, "crossover must be OX|PMX|CX|ERX, got '" + tok + "'");
}

GeneticMutationType parseMutation(const std::string& tok,
                                  const std::string& path,
                                  std::size_t line_no) {
    const std::string up = upperCopy(tok);
    if (up == "SWAP")      return GeneticMutationType::SWAP;
    if (up == "INVERSION" || up == "INV") return GeneticMutationType::INVERSION;
    if (up == "INSERTION" || up == "INS") return GeneticMutationType::INSERTION;
    if (up == "SCRAMBLE")  return GeneticMutationType::SCRAMBLE;
    if (up == "RANDOM")    return GeneticMutationType::RANDOM;
    fail(path, line_no, "mutation must be swap|inversion|insertion|scramble|random, got '" + tok + "'");
}

GeneticInitMode parseInit(const std::string& tok,
                          const std::string& path,
                          std::size_t line_no) {
    const std::string up = upperCopy(tok);
    if (up == "RANDOM")   return GeneticInitMode::RANDOM;
    if (up == "GREEDY")   return GeneticInitMode::GREEDY;
    if (up == "RGREEDY")  return GeneticInitMode::RGREEDY;
    fail(path, line_no, "init must be random|greedy|rgreedy, got '" + tok + "'");
}

} // namespace

GeneticAlgorithmConfig2 GaVariant2::toConfig(std::uint64_t seed) const {
    GeneticAlgorithmConfig2 cfg;
    cfg.population_size = population;
    cfg.generations = generations;
    cfg.mutation_rate = mutation_rate;
    cfg.crossover_rate = crossover_rate;
    cfg.elitism_count = elitism_count;
    cfg.tournament_size = tournament_size;
    cfg.seed = seed;
    cfg.crossover_type = crossover_type;
    cfg.mutation_type = mutation_type;
    cfg.selection_type = selection_type;
    cfg.early_stop_generations = early_stop_generations;
    cfg.init_mode = init_mode;
    cfg.two_opt_frequency = two_opt_frequency;
    return cfg;
}

std::vector<GaVariant2> Ga2ConfigFile::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open GA2 config file: " + path);

    std::vector<GaVariant2> out;
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
        if (f.size() > 13)
            fail(path, line_no, "too many fields (max 13: label,population,"
                                "generations,mutation_rate,crossover_rate,"
                                "elitism,tournament,selection,crossover,"
                                "mutation,early_stop,init,two_opt)");

        GaVariant2 v; // trailing fields left empty fall back to these defaults
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
            if (v.tournament_size == 0) fail(path, line_no, "tournament must be >= 1");
        }
        if (f.size() > 7 && !f[7].empty())
            v.selection_type = parseEnumField(f[7], path, line_no, "selection", parseSelection);
        if (f.size() > 8 && !f[8].empty())
            v.crossover_type = parseEnumField(f[8], path, line_no, "crossover", parseCrossover2);
        if (f.size() > 9 && !f[9].empty())
            v.mutation_type = parseEnumField(f[9], path, line_no, "mutation", parseMutation);
        if (f.size() > 10 && !f[10].empty())
            v.early_stop_generations = parseSizeField(f[10], path, line_no, "early_stop");
        if (f.size() > 11 && !f[11].empty())
            v.init_mode = parseEnumField(f[11], path, line_no, "init", parseInit);
        if (f.size() > 12 && !f[12].empty())
            v.two_opt_frequency = parseSizeField(f[12], path, line_no, "two_opt");

        if (!seen.insert(v.label).second)
            fail(path, line_no, "duplicate label '" + v.label + "'");
        out.push_back(std::move(v));
    }
    if (out.empty())
        throw std::runtime_error(path + ": no configs found (need at least one row)");
    return out;
}

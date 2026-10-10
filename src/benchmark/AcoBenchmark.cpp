// AcoBenchmark.cpp - implementation of the ACO-vs-Greedy benchmark building
// blocks declared in AcoBenchmark.h.  The CSV parsing mirrors GaConfigFile;
// the shared aggregation/reporting pieces come from GaBenchmark.{h,cpp}.

#include "AcoBenchmark.h"

#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// AcoVariant
// ---------------------------------------------------------------------------
AntColonyConfig AcoVariant::toConfig(std::uint64_t seed) const {
    AntColonyConfig cfg;
    cfg.ant_count = ants;
    cfg.iterations = iterations;
    cfg.alpha = alpha;
    cfg.beta = beta;
    cfg.rho = rho;
    cfg.q = q;
    cfg.seed = seed;
    return cfg;
}

// ---------------------------------------------------------------------------
// Shared CSV-parsing helpers for AcoConfigFile (same rules as GaConfigFile)
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

// Returns the part before '#'.
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

} // namespace

// ---------------------------------------------------------------------------
// AcoConfigFile
// ---------------------------------------------------------------------------
std::vector<AcoVariant> AcoConfigFile::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open ACO config file: " + path);

    std::vector<AcoVariant> out;
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
        if (f.size() > 7)
            fail(path, line_no, "too many fields (max 7: label,ants,iterations,"
                                "alpha,beta,rho,q)");

        AcoVariant v; // trailing fields left empty fall back to these defaults
        v.label = f[0];
        if (f.size() > 1 && !f[1].empty()) v.ants = parseSizeField(f[1], path, line_no, "ants");
        if (f.size() > 2 && !f[2].empty()) v.iterations = parseSizeField(f[2], path, line_no, "iterations");
        if (v.ants == 0 || v.iterations == 0)
            fail(path, line_no, "ants and iterations are required (>= 1);"
                                " the compute budget is part of the config row");
        if (f.size() > 3 && !f[3].empty()) {
            v.alpha = parseDoubleField(f[3], path, line_no, "alpha");
            if (!(v.alpha >= 0.0))
                fail(path, line_no, "alpha must be >= 0");
        }
        if (f.size() > 4 && !f[4].empty()) {
            v.beta = parseDoubleField(f[4], path, line_no, "beta");
            if (!(v.beta >= 0.0))
                fail(path, line_no, "beta must be >= 0");
        }
        if (f.size() > 5 && !f[5].empty()) {
            v.rho = parseDoubleField(f[5], path, line_no, "rho");
            if (!(v.rho >= 0.0 && v.rho <= 1.0))
                fail(path, line_no, "rho must be in [0,1]");
        }
        if (f.size() > 6 && !f[6].empty()) {
            v.q = parseDoubleField(f[6], path, line_no, "q");
            if (!(v.q > 0.0))
                fail(path, line_no, "q must be > 0");
        }

        if (!seen.insert(v.label).second)
            fail(path, line_no, "duplicate label '" + v.label + "'");
        out.push_back(std::move(v));
    }
    if (out.empty())
        throw std::runtime_error(path + ": no configs found (need at least one row)");
    return out;
}

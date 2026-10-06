#pragma once

// ConfigEcho.h
//
// Appends a "Configs tested" section - one row per benchmark config with its
// full parameter set - to a benchmark summary file and stream, so that
// readers of the summary alone can see exactly which configs were run.

#include <ostream>
#include <string>
#include <utility>
#include <vector>

// rows: (label, human-readable parameter description).
// Appends to the summary file at `summary_path` and streams the same text to
// `out`; returns nothing (pure side effect).
void writeConfigEcho(
    std::ostream& out,
    const std::string& summary_path,
    const std::vector<std::pair<std::string, std::string>>& rows
);

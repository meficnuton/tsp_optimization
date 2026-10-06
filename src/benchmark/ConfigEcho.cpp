// ConfigEcho.cpp - appends the "Configs tested" section to a summary.

#include "ConfigEcho.h"

#include <fstream>
#include <iomanip>
#include <sstream>

void writeConfigEcho(
    std::ostream& out,
    const std::string& summary_path,
    const std::vector<std::pair<std::string, std::string>>& rows
) {
    std::ostringstream body;
    body << "=== Configs tested (echo of the config file) ===\n";
    body << std::left << std::setw(32) << "label" << " parameters\n";
    for (const auto& [label, description] : rows) {
        body << std::left << std::setw(32) << label << ' ' << description << '\n';
    }
    body << '\n';

    out << body.str();
    std::ofstream file(summary_path, std::ios::app);
    file << body.str();
}

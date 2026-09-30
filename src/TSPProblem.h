#pragma once

#include <cstddef>
#include <string>
#include <vector>

class TSPProblem {
public:
    TSPProblem() = default;

    TSPProblem(
        std::string name,
        std::string comment,
        std::vector<std::vector<double>> distance_matrix
    );

    static TSPProblem fromTspFile(const std::string& filename);

    // Metadata
    const std::string& name() const noexcept;
    const std::string& comment() const noexcept;
    std::size_t dimension() const noexcept;

    // Distance access
    double distance(std::size_t from, std::size_t to) const;

    const std::vector<std::vector<double>>& distanceMatrix() const noexcept;

    // Tour utilities
    double tourCost(const std::vector<int>& tour) const;

private:
    std::string name_;
    std::string comment_;

    std::vector<std::vector<double>> distance_matrix_;
};

#include "GreedySolver.h"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

SolverResult GreedySolver::solve(
    const TSPProblem& problem,
    const SolverConfig& config
)
{
    const auto started = std::chrono::steady_clock::now();

    const std::size_t dimension = problem.dimension();
    std::size_t start_vertex = 0;
    if (const auto* greedy_config = dynamic_cast<const GreedyConfig*>(&config)) {
        start_vertex = greedy_config->start_vertex;
    }

    if (dimension != 0 && start_vertex >= dimension) {
        throw std::out_of_range("GreedySolver start vertex is outside the problem");
    }

    SolverResult result;
    result.algorithm = name();
    result.tour.reserve(dimension);

    if (dimension != 0) {
        std::vector<bool> visited(dimension, false);
        std::size_t current = start_vertex;
        result.tour.push_back(static_cast<int>(current));
        visited[current] = true;

        while (result.tour.size() < dimension) {
            std::size_t next = dimension;
            double best_distance = std::numeric_limits<double>::infinity();

            // Iterating in index order makes equal-distance choices reproducible.
            for (std::size_t candidate = 0; candidate < dimension; ++candidate) {
                if (visited[candidate]) {
                    continue;
                }

                const double candidate_distance = problem.distance(current, candidate);
                if (next == dimension || candidate_distance < best_distance) {
                    next = candidate;
                    best_distance = candidate_distance;
                }
            }

            current = next;
            visited[current] = true;
            result.tour.push_back(static_cast<int>(current));
        }
    }

    result.cost = problem.tourCost(result.tour);
    const auto finished = std::chrono::steady_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

std::string GreedySolver::name() const
{
    return "Greedy";
}

// TourConstruction.cpp - greedy / randomized-greedy construction and 2-opt.

#include "TourConstruction.h"

#include <algorithm>
#include <numeric>

std::vector<int> greedyTour(const TSPProblem& problem, std::size_t start) {
    const std::size_t n = problem.dimension();
    std::vector<int> tour;
    tour.reserve(n);
    if (n == 0) return tour;
    if (start >= n) start = 0;

    std::vector<char> visited(n, 0);
    std::size_t current = start;
    tour.push_back(static_cast<int>(current));
    visited[current] = 1;
    for (std::size_t step = 1; step < n; ++step) {
        std::size_t best = n;
        double best_cost = 0.0;
        for (std::size_t city = 0; city < n; ++city) {
            if (visited[city]) continue;
            const double cost = problem.distance(current, city);
            if (best == n || cost < best_cost) {
                best = city;
                best_cost = cost;
            }
        }
        visited[best] = 1;
        tour.push_back(static_cast<int>(best));
        current = best;
    }
    return tour;
}

std::vector<int> randomizedGreedyTour(
    const TSPProblem& problem,
    std::size_t start,
    std::size_t candidate_count,
    std::mt19937_64& random
) {
    const std::size_t n = problem.dimension();
    if (candidate_count <= 1 || n == 0) {
        return greedyTour(problem, start);
    }
    if (start >= n) start = 0;

    std::vector<int> tour;
    tour.reserve(n);
    std::vector<char> visited(n, 0);
    std::size_t current = start;
    tour.push_back(static_cast<int>(current));
    visited[current] = 1;

    std::vector<std::pair<double, std::size_t>> candidates;
    candidates.reserve(n);
    std::uniform_int_distribution<std::size_t> pick(0, 0);

    for (std::size_t step = 1; step < n; ++step) {
        candidates.clear();
        for (std::size_t city = 0; city < n; ++city) {
            if (visited[city]) continue;
            candidates.emplace_back(problem.distance(current, city), city);
        }
        const std::size_t k = std::min(candidate_count, candidates.size());
        const std::size_t from = candidates.size() - k;
        std::nth_element(candidates.begin(), candidates.begin() + from,
                         candidates.end());
        pick = std::uniform_int_distribution<std::size_t>(from,
                                                         candidates.size() - 1);
        const auto [dist, city] = candidates[pick(random)];
        (void)dist;
        visited[city] = 1;
        tour.push_back(static_cast<int>(city));
        current = city;
    }
    return tour;
}

double twoOptImprove(const TSPProblem& problem, std::vector<int>& tour) {
    const std::size_t n = tour.size();
    if (n < 4) return 0.0;
    const std::vector<std::vector<double>>& m = problem.distanceMatrix();

    double total_gain = 0.0;
    bool improved = true;
    while (improved) {
        improved = false;
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const int a = tour[i];
            const int b = tour[(i + 1) % n];
            double best_delta = 0.0;   // strictly negative means improving
            std::size_t best_j = 0;
            for (std::size_t j = i + 2; j < n; ++j) {
                if (i == 0 && j == n - 1) continue; // reversing changes nothing
                const int c = tour[j];
                const int d = tour[(j + 1) % n];
                const double delta =
                    m[static_cast<std::size_t>(a)][static_cast<std::size_t>(c)] +
                    m[static_cast<std::size_t>(b)][static_cast<std::size_t>(d)] -
                    m[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] -
                    m[static_cast<std::size_t>(c)][static_cast<std::size_t>(d)];
                if (delta < best_delta - 1e-9) {
                    best_delta = delta;
                    best_j = j;
                }
            }
            if (best_j != 0) {
                std::reverse(tour.begin() + static_cast<std::ptrdiff_t>(i + 1),
                             tour.begin() + static_cast<std::ptrdiff_t>(best_j + 1));
                total_gain -= best_delta;
                improved = true;
            }
        }
    }
    return total_gain;
}

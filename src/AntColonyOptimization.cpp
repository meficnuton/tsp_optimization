#include "AntColonyOptimization.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

namespace {
} // namespace

SolverResult AntColonyOptimization::solve(
    const TSPProblem& problem,
    const SolverConfig& config
)
{
    const auto started = std::chrono::steady_clock::now();

    const std::size_t dimension = problem.dimension();
    AntColonyConfig aco_config;
    if (const auto* cfg = dynamic_cast<const AntColonyConfig*>(&config)) {
        aco_config = *cfg;
    }

    if (aco_config.ant_count == 0 || aco_config.iterations == 0) {
        throw std::invalid_argument(
            "AntColonyConfig requires ant_count >= 1 and iterations >= 1");
    }
    if (aco_config.rho < 0.0 || aco_config.rho > 1.0) {
        throw std::invalid_argument("AntColonyConfig rho must be in [0,1]");
    }

    SolverResult result;
    result.algorithm = name();

    if (dimension == 0) {
        const auto finished = std::chrono::steady_clock::now();
        result.time_ms = std::chrono::duration<double, std::milli>(finished - started).count();
        return result;
    }
    if (dimension == 1) {
        result.tour = {0};
        result.cost = 0.0;
        const auto finished = std::chrono::steady_clock::now();
        result.time_ms = std::chrono::duration<double, std::milli>(finished - started).count();
        return result;
    }

    // Reproducible when seeded, non-deterministic when seed == 0.
    std::mt19937_64 rng(
        aco_config.seed != 0
            ? aco_config.seed
            : static_cast<std::uint64_t>(std::random_device{}()));

    const std::size_t n = dimension;

    // Heuristic desirability eta = 1 / distance, precomputed once.
    std::vector<double> eta(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            const double d = problem.distance(i, j);
            eta[i * n + j] = d > 0.0 ? 1.0 / d : std::numeric_limits<double>::max();
        }
    }

    // Initial pheromone from a nearest-neighbour tour (classic AS rule):
    // tau0 = ant_count / L_nn.
    std::vector<std::size_t> nn_tour(n);
    {
        std::vector<bool> visited(n, false);
        std::size_t current = 0;
        nn_tour[0] = current;
        visited[0] = true;
        for (std::size_t step = 1; step < n; ++step) {
            std::size_t best = n;
            double best_d = std::numeric_limits<double>::infinity();
            for (std::size_t candidate = 0; candidate < n; ++candidate) {
                if (visited[candidate]) continue;
                const double d = problem.distance(current, candidate);
                if (d < best_d) {
                    best = candidate;
                    best_d = d;
                }
            }
            current = best;
            visited[current] = true;
            nn_tour[step] = current;
        }
    }
    double nn_cost = 0.0;
    for (std::size_t step = 0; step < n; ++step) {
        const std::size_t a = nn_tour[step];
        const std::size_t b = nn_tour[(step + 1) % n];
        nn_cost += problem.distance(a, b);
    }
    const double tau0 = nn_cost > 0.0
        ? static_cast<double>(aco_config.ant_count) / nn_cost
        : 1.0;

    std::vector<double> tau(n * n, tau0);
    std::vector<double> weight(n * n, 0.0); // tau^alpha * eta^beta scratch
    std::vector<std::vector<std::size_t>> ant_tours(aco_config.ant_count,
                                                    std::vector<std::size_t>(n));
    std::vector<double> ant_costs(aco_config.ant_count, 0.0);

    std::vector<std::size_t> best_tour;
    double best_cost = std::numeric_limits<double>::infinity();

    std::vector<double> cumulative(n);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    for (std::size_t iteration = 0; iteration < aco_config.iterations; ++iteration) {
        // ---- each ant constructs a tour by roulette wheel ------------------
        for (std::size_t ant = 0; ant < aco_config.ant_count; ++ant) {
            std::vector<bool> visited(n, false);
            std::size_t start = rng() % n; // every city gets to be first
            std::size_t current = start;
            visited[current] = true;
            ant_tours[ant][0] = current;
            double cost = 0.0;

            for (std::size_t step = 1; step < n; ++step) {
                // Refresh the edge weights leaving the current city.
                double total = 0.0;
                for (std::size_t candidate = 0; candidate < n; ++candidate) {
                    if (visited[candidate]) continue;
                    const double w =
                        std::pow(tau[current * n + candidate], aco_config.alpha) *
                        std::pow(eta[current * n + candidate], aco_config.beta);
                    weight[candidate] = w;
                    total += w;
                }

                // Roulette wheel over unvisited cities; uniform fallback when
                // every weight underflows to zero.
                std::size_t next = n;
                if (total > 0.0) {
                    const double pick = uniform(rng) * total;
                    double acc = 0.0;
                    for (std::size_t candidate = 0; candidate < n; ++candidate) {
                        if (visited[candidate]) continue;
                        acc += weight[candidate];
                        if (pick <= acc) {
                            next = candidate;
                            break;
                        }
                    }
                    if (next == n) next = ant_tours[ant][step - 1]; // unreachable, but safe
                } else {
                    std::size_t unvisited = 0;
                    for (std::size_t candidate = 0; candidate < n; ++candidate)
                        if (!visited[candidate]) ++unvisited;
                    std::size_t pick = rng() % unvisited;
                    for (std::size_t candidate = 0; candidate < n; ++candidate) {
                        if (visited[candidate]) continue;
                        if (pick-- == 0) {
                            next = candidate;
                            break;
                        }
                    }
                }

                visited[next] = true;
                ant_tours[ant][step] = next;
                cost += problem.distance(current, next);
                current = next;
            }
            cost += problem.distance(ant_tours[ant][n - 1], ant_tours[ant][0]);
            ant_costs[ant] = cost;

            if (cost < best_cost) {
                best_cost = cost;
                best_tour = ant_tours[ant];
            }
        }

        // ---- pheromone evaporation -----------------------------------------
        const double keep = 1.0 - aco_config.rho;
        for (double& t : tau) t *= keep;

        // ---- pheromone deposit: every ant lays q / L on its edges -----------
        for (std::size_t ant = 0; ant < aco_config.ant_count; ++ant) {
            const double deposit = aco_config.q / ant_costs[ant];
            for (std::size_t step = 0; step < n; ++step) {
                const std::size_t a = ant_tours[ant][step];
                const std::size_t b = ant_tours[ant][(step + 1) % n];
                tau[a * n + b] += deposit;
                tau[b * n + a] += deposit;
            }
        }
    }

    result.tour.reserve(n);
    for (const std::size_t city : best_tour)
        result.tour.push_back(static_cast<int>(city));
    result.cost = problem.tourCost(result.tour);
    const auto finished = std::chrono::steady_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

std::string AntColonyOptimization::name() const
{
    return "ACO";
}

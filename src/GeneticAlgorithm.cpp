#include "GeneticAlgorithm.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using Tour = std::vector<int>;

struct Individual {
    Tour tour;
    double cost = std::numeric_limits<double>::infinity();
};

bool better(const Individual& lhs, const Individual& rhs)
{
    return lhs.cost < rhs.cost;
}

Individual make_individual(const TSPProblem& problem, Tour tour)
{
    Individual result;
    result.tour = std::move(tour);
    result.cost = problem.tourCost(result.tour);
    return result;
}

// Pick the best member of a uniformly sampled tournament.
const Individual& tournament_select(
    const std::vector<Individual>& population,
    std::size_t tournament_size,
    std::mt19937_64& random
)
{
    std::uniform_int_distribution<std::size_t> index(0, population.size() - 1);
    const Individual* winner = &population[index(random)];
    for (std::size_t i = 1; i < tournament_size; ++i) {
        const Individual& candidate = population[index(random)];
        if (better(candidate, *winner)) {
            winner = &candidate;
        }
    }
    return *winner;
}

// Ordered crossover (OX), which always produces a permutation without
// duplicate vertices.
Tour ordered_crossover(const Tour& first, const Tour& second,
                       std::mt19937_64& random)
{
    const std::size_t n = first.size();
    if (n == 0) {
        return {};
    }
    if (n == 1) {
        return Tour{first.front()};
    }

    std::uniform_int_distribution<std::size_t> cut(0, n - 1);
    std::size_t left = cut(random);
    std::size_t right = cut(random);
    if (left > right) {
        std::swap(left, right);
    }

    Tour child(n, -1);
    std::vector<bool> used(n, false);
    for (std::size_t i = left; i <= right; ++i) {
        child[i] = first[i];
        used[static_cast<std::size_t>(child[i])] = true;
    }

    std::size_t position = (right + 1) % n;
    for (std::size_t offset = 0; offset < n; ++offset) {
        const int vertex = second[(right + 1 + offset) % n];
        if (used[static_cast<std::size_t>(vertex)]) {
            continue;
        }
        child[position] = vertex;
        used[static_cast<std::size_t>(vertex)] = true;
        position = (position + 1) % n;
    }
    return child;
}

// Partially mapped crossover (PMX). The selected segment is inherited from
// the first parent; conflicting vertices from the second parent are resolved
// through the mapping induced by that segment.
Tour partially_mapped_crossover(const Tour& first, const Tour& second,
                                std::mt19937_64& random)
{
    const std::size_t n = first.size();
    if (n == 0) {
        return {};
    }
    if (n == 1) {
        return Tour{first.front()};
    }

    std::uniform_int_distribution<std::size_t> cut(0, n - 1);
    std::size_t left = cut(random);
    std::size_t right = cut(random);
    if (left > right) {
        std::swap(left, right);
    }

    Tour child(n, -1);
    std::vector<bool> used(n, false);
    std::vector<std::size_t> position_in_first(n);
    for (std::size_t i = 0; i < n; ++i) {
        position_in_first[static_cast<std::size_t>(first[i])] = i;
    }

    for (std::size_t i = left; i <= right; ++i) {
        child[i] = first[i];
        used[static_cast<std::size_t>(first[i])] = true;
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (i >= left && i <= right) {
            continue;
        }

        int vertex = second[i];
        while (used[static_cast<std::size_t>(vertex)]) {
            const std::size_t mapped_position =
                position_in_first[static_cast<std::size_t>(vertex)];
            vertex = second[mapped_position];
        }
        child[i] = vertex;
    }
    return child;
}

Tour crossover(const Tour& first, const Tour& second,
               GeneticCrossoverType crossover_type,
               std::mt19937_64& random)
{
    switch (crossover_type) {
    case GeneticCrossoverType::OX:
        return ordered_crossover(first, second, random);
    case GeneticCrossoverType::PMX:
        return partially_mapped_crossover(first, second, random);
    }
    throw std::invalid_argument("Unknown GeneticAlgorithm crossover type");
}

void mutate(Tour& tour, double mutation_rate, std::mt19937_64& random)
{
    if (tour.size() < 2) {
        return;
    }
    std::bernoulli_distribution should_mutate(mutation_rate);
    if (should_mutate(random)) {
        std::uniform_int_distribution<std::size_t> index(0, tour.size() - 1);
        const std::size_t first = index(random);
        const std::size_t second = index(random);
        std::swap(tour[first], tour[second]);
    }
}

} // namespace

SolverResult GeneticAlgorithm::solve(
    const TSPProblem& problem,
    const SolverConfig& config
)
{
    const auto started = std::chrono::steady_clock::now();

    GeneticAlgorithmConfig settings;
    if (const auto* genetic_config = dynamic_cast<const GeneticAlgorithmConfig*>(&config)) {
        settings = *genetic_config;
    }

    if (settings.population_size == 0) {
        throw std::invalid_argument("GeneticAlgorithm population size must be greater than zero");
    }
    if (!std::isfinite(settings.mutation_rate) || settings.mutation_rate < 0.0 ||
        settings.mutation_rate > 1.0) {
        throw std::invalid_argument("GeneticAlgorithm mutation rate must be in [0, 1]");
    }
    if (!std::isfinite(settings.crossover_rate) || settings.crossover_rate < 0.0 ||
        settings.crossover_rate > 1.0) {
        throw std::invalid_argument("GeneticAlgorithm crossover rate must be in [0, 1]");
    }
    if (settings.crossover_type != GeneticCrossoverType::OX &&
        settings.crossover_type != GeneticCrossoverType::PMX) {
        throw std::invalid_argument("Unknown GeneticAlgorithm crossover type");
    }

    const std::size_t dimension = problem.dimension();
    SolverResult result;
    result.algorithm = name();
    result.tour.reserve(dimension);

    // Empty and singleton instances have no meaningful evolutionary search.
    if (dimension == 0) {
        result.cost = problem.tourCost(result.tour);
        const auto finished = std::chrono::steady_clock::now();
        result.time_ms = std::chrono::duration<double, std::milli>(finished - started).count();
        return result;
    }
    if (dimension == 1) {
        result.tour.push_back(0);
        result.cost = problem.tourCost(result.tour);
        const auto finished = std::chrono::steady_clock::now();
        result.time_ms = std::chrono::duration<double, std::milli>(finished - started).count();
        return result;
    }

    if (settings.elitism_count > settings.population_size) {
        settings.elitism_count = settings.population_size;
    }
    if (settings.tournament_size == 0) {
        settings.tournament_size = 1;
    }
    settings.tournament_size = std::min(settings.tournament_size, settings.population_size);

    std::mt19937_64 random;
    if (settings.seed == 0) {
        std::random_device device;
        random.seed((static_cast<std::uint64_t>(device()) << 32) ^ device());
    } else {
        random.seed(settings.seed);
    }

    Tour base(dimension);
    std::iota(base.begin(), base.end(), 0);
    std::vector<Individual> population;
    population.reserve(settings.population_size);
    population.push_back(make_individual(problem, base));

    // Shuffling a fresh permutation for each remaining member gives the
    // initial population broad coverage while retaining one deterministic tour.
    for (std::size_t i = 1; i < settings.population_size; ++i) {
        Tour tour = base;
        std::shuffle(tour.begin(), tour.end(), random);
        population.push_back(make_individual(problem, std::move(tour)));
    }


    
    for (std::size_t generation = 0; generation < settings.generations; ++generation) {
        // elitism
        std::sort(population.begin(), population.end(), better);
        std::vector<Individual> next;
        next.reserve(settings.population_size);
        for (std::size_t i = 0; i < settings.elitism_count; ++i) {
            next.push_back(population[i]);
        }

        std::bernoulli_distribution should_crossover(settings.crossover_rate);
        while (next.size() < settings.population_size) {
            const Individual& first = tournament_select(population, settings.tournament_size, random);
            const Individual& second = tournament_select(population, settings.tournament_size, random);
            Tour child = should_crossover(random)
                ? crossover(first.tour, second.tour, settings.crossover_type, random)
                : first.tour;
            mutate(child, settings.mutation_rate, random);
            next.push_back(make_individual(problem, std::move(child)));
        }
        population = std::move(next);
    }

    std::sort(population.begin(), population.end(), better);
    result.tour = population.front().tour;
    result.cost = population.front().cost;
    const auto finished = std::chrono::steady_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

std::string GeneticAlgorithm::name() const
{
    return "Genetic Algorithm";
}

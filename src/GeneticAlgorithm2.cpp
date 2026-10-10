// GeneticAlgorithm2.cpp - extended genetic TSP solver (see GeneticAlgorithm2.h).

#include "GeneticAlgorithm2.h"

#include "TourConstruction.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
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

// ---------------------------------------------------------------------------
// Crossover operators (all preserve the permutation property).
// ---------------------------------------------------------------------------

// Ordered crossover (OX).
Tour ordered_crossover(const Tour& first, const Tour& second,
                       std::mt19937_64& random)
{
    const std::size_t n = first.size();
    if (n == 0) return {};
    if (n == 1) return Tour{first.front()};

    std::uniform_int_distribution<std::size_t> cut(0, n - 1);
    std::size_t left = cut(random);
    std::size_t right = cut(random);
    if (left > right) std::swap(left, right);

    Tour child(n, -1);
    std::vector<bool> used(n, false);
    for (std::size_t i = left; i <= right; ++i) {
        child[i] = first[i];
        used[static_cast<std::size_t>(child[i])] = true;
    }

    std::size_t position = (right + 1) % n;
    for (std::size_t offset = 0; offset < n; ++offset) {
        const int vertex = second[(right + 1 + offset) % n];
        if (used[static_cast<std::size_t>(vertex)]) continue;
        child[position] = vertex;
        used[static_cast<std::size_t>(vertex)] = true;
        position = (position + 1) % n;
    }
    return child;
}

// Partially mapped crossover (PMX).
Tour partially_mapped_crossover(const Tour& first, const Tour& second,
                                std::mt19937_64& random)
{
    const std::size_t n = first.size();
    if (n == 0) return {};
    if (n == 1) return Tour{first.front()};

    std::uniform_int_distribution<std::size_t> cut(0, n - 1);
    std::size_t left = cut(random);
    std::size_t right = cut(random);
    if (left > right) std::swap(left, right);

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
        if (i >= left && i <= right) continue;
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

// Cycle crossover (CX): every city keeps the position it has in one of the
// two parents; the positions of alternating cycles come from parent 1.
Tour cycle_crossover(const Tour& first, const Tour& second)
{
    const std::size_t n = first.size();
    if (n == 0) return {};
    if (n == 1) return Tour{first.front()};

    std::vector<std::size_t> position_in_first(n);
    for (std::size_t i = 0; i < n; ++i) {
        position_in_first[static_cast<std::size_t>(first[i])] = i;
    }

    Tour child(n, -1);
    std::vector<bool> in_cycle(n, false);
    std::size_t start = 0;
    for (std::size_t attempt = 0; attempt < n; ++attempt) {
        if (!in_cycle[attempt]) { start = attempt; break; }
    }
    std::size_t position = start;
    do {
        in_cycle[position] = true;
        child[position] = first[position];
        position = position_in_first[static_cast<std::size_t>(second[position])];
    } while (position != start);

    for (std::size_t i = 0; i < n; ++i) {
        if (!in_cycle[i]) child[i] = second[i];
    }
    return child;
}

// Edge recombination crossover (ERX): builds a child from the union of the
// parents' adjacency (cyclic) edges, always extending to the neighbour with
// the fewest remaining edges (ties broken randomly).
Tour edge_recombination_crossover(const Tour& first, const Tour& second,
                                  std::mt19937_64& random)
{
    const std::size_t n = first.size();
    if (n == 0) return {};
    if (n == 1) return Tour{first.front()};

    std::vector<std::vector<int>> neighbours(n);
    const auto add_edge = [&](int a, int b) {
        auto& list = neighbours[static_cast<std::size_t>(a)];
        if (std::find(list.begin(), list.end(), b) == list.end()) {
            list.push_back(b);
        }
    };
    for (std::size_t i = 0; i < n; ++i) {
        const int a = first[i];
        const int b = first[(i + 1) % n];
        const int c = second[i];
        const int d = second[(i + 1) % n];
        add_edge(a, b); add_edge(b, a);
        add_edge(c, d); add_edge(d, c);
    }

    std::vector<char> alive(n, 1);
    std::uniform_int_distribution<std::size_t> any_city(0, n - 1);
    Tour child;
    child.reserve(n);

    int current = static_cast<int>(any_city(random));
    alive[static_cast<std::size_t>(current)] = 0;
    child.push_back(current);

    for (std::size_t step = 1; step < n; ++step) {
        // Prune `current` from every neighbour list.
        for (int nb : neighbours[static_cast<std::size_t>(current)]) {
            auto& list = neighbours[static_cast<std::size_t>(nb)];
            list.erase(std::remove(list.begin(), list.end(), current),
                       list.end());
        }

        // Candidate extensions: alive neighbours of `current`, grouped by
        // their own remaining alive-neighbour count (fewest first).
        int best = -1;
        std::size_t best_degree = 0;
        std::vector<int> ties;
        for (int nb : neighbours[static_cast<std::size_t>(current)]) {
            if (!alive[static_cast<std::size_t>(nb)]) continue;
            const std::size_t degree = neighbours[static_cast<std::size_t>(nb)].size();
            if (best == -1 || degree < best_degree) {
                best = nb;
                best_degree = degree;
                ties.clear();
                ties.push_back(nb);
            } else if (degree == best_degree) {
                ties.push_back(nb);
            }
        }

        if (best == -1) {
            // No common edge available: jump to a random unused city.
            do {
                best = static_cast<int>(any_city(random));
            } while (!alive[static_cast<std::size_t>(best)]);
        } else if (ties.size() > 1) {
            std::uniform_int_distribution<std::size_t> tie(0, ties.size() - 1);
            best = ties[tie(random)];
        }

        alive[static_cast<std::size_t>(best)] = 0;
        child.push_back(best);
        current = best;
    }
    return child;
}

Tour crossover(const Tour& first, const Tour& second,
               GeneticCrossoverType2 crossover_type,
               std::mt19937_64& random)
{
    switch (crossover_type) {
    case GeneticCrossoverType2::OX:  return ordered_crossover(first, second, random);
    case GeneticCrossoverType2::PMX: return partially_mapped_crossover(first, second, random);
    case GeneticCrossoverType2::CX:  return cycle_crossover(first, second);
    case GeneticCrossoverType2::ERX: return edge_recombination_crossover(first, second, random);
    }
    throw std::invalid_argument("Unknown GeneticAlgorithm2 crossover type");
}

// ---------------------------------------------------------------------------
// Mutation operators (each call applies ONE random-move event).
// ---------------------------------------------------------------------------

void apply_mutation(Tour& tour, GeneticMutationType type,
                    std::mt19937_64& random)
{
    const std::size_t n = tour.size();
    if (n < 2) return;
    std::uniform_int_distribution<std::size_t> index(0, n - 1);

    GeneticMutationType op = type;
    if (op == GeneticMutationType::RANDOM) {
        const std::size_t choice = std::uniform_int_distribution<std::size_t>(
            0, 3)(random);
        op = static_cast<GeneticMutationType>(choice); // SWAP..SCRAMBLE order
    }

    switch (op) {
    case GeneticMutationType::SWAP: {
        const std::size_t a = index(random);
        const std::size_t b = index(random);
        std::swap(tour[a], tour[b]);
        break;
    }
    case GeneticMutationType::INVERSION: {
        std::size_t a = index(random);
        std::size_t b = index(random);
        if (a > b) std::swap(a, b);
        std::reverse(tour.begin() + static_cast<std::ptrdiff_t>(a),
                     tour.begin() + static_cast<std::ptrdiff_t>(b + 1));
        break;
    }
    case GeneticMutationType::INSERTION: {
        const std::size_t from = index(random);
        std::size_t to = index(random);
        if (to == from) to = (from + 1) % n;
        const int city = tour[from];
        tour.erase(tour.begin() + static_cast<std::ptrdiff_t>(from));
        tour.insert(tour.begin() + static_cast<std::ptrdiff_t>(to), city);
        break;
    }
    case GeneticMutationType::SCRAMBLE: {
        std::size_t a = index(random);
        std::size_t b = index(random);
        if (a > b) std::swap(a, b);
        std::shuffle(tour.begin() + static_cast<std::ptrdiff_t>(a),
                     tour.begin() + static_cast<std::ptrdiff_t>(b + 1), random);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Selection schemes. All return indices into `population`.
// ---------------------------------------------------------------------------

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
        if (better(candidate, *winner)) winner = &candidate;
    }
    return *winner;
}

// Roulette over inverse costs. Returns an individual by sampling weights
// w_i = 1 / (cost_i - best_cost + eps).
class RouletteSelector {
public:
    explicit RouletteSelector(const std::vector<Individual>& population) {
        double best = population.front().cost;
        for (const auto& ind : population) best = std::min(best, ind.cost);
        const double eps = std::max(1e-9, 1e-6 * std::fabs(best));
        std::vector<double> weights;
        weights.reserve(population.size());
        for (const auto& ind : population) {
            weights.push_back(1.0 / (ind.cost - best + eps));
        }
        distribution_ = std::discrete_distribution<std::size_t>(
            weights.begin(), weights.end());
    }
    std::size_t pick(std::mt19937_64& random) {
        return distribution_(random);
    }
private:
    std::discrete_distribution<std::size_t> distribution_;
};

// Linear rank selection: sorted rank r (0 = worst) gets weight r + 1.
class RankSelector {
public:
    explicit RankSelector(const std::vector<Individual>& population) {
        std::vector<std::size_t> order(population.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return population[a].cost < population[b].cost;
        });
        std::vector<double> weights(population.size());
        for (std::size_t r = 0; r < order.size(); ++r) {
            weights[order[r]] = static_cast<double>(r + 1);
        }
        distribution_ = std::discrete_distribution<std::size_t>(
            weights.begin(), weights.end());
    }
    std::size_t pick(std::mt19937_64& random) {
        return distribution_(random);
    }
private:
    std::discrete_distribution<std::size_t> distribution_;
};

// Round-robin tournament: every individual meets `pressure` random opponents
// and wins one point per better opponent; parents are then sampled
// proportional to (wins + 1) so zero-win individuals stay reachable.
class RoundRobinSelector {
public:
    RoundRobinSelector(const std::vector<Individual>& population,
                       std::size_t pressure, std::mt19937_64& random) {
        const std::size_t n = population.size();
        std::vector<double> weights(n, 1.0);
        std::uniform_int_distribution<std::size_t> index(0, n - 1);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t round = 0; round < pressure; ++round) {
                const std::size_t opponent = index(random);
                if (population[i].cost < population[opponent].cost) {
                    weights[i] += 1.0;
                }
            }
        }
        distribution_ = std::discrete_distribution<std::size_t>(
            weights.begin(), weights.end());
    }
    std::size_t pick(std::mt19937_64& random) {
        return distribution_(random);
    }
private:
    std::discrete_distribution<std::size_t> distribution_;
};

} // namespace

// ---------------------------------------------------------------------------
// GeneticAlgorithm2::solve
// ---------------------------------------------------------------------------
SolverResult GeneticAlgorithm2::solve(
    const TSPProblem& problem,
    const SolverConfig& config
)
{
    const auto started = std::chrono::steady_clock::now();

    GeneticAlgorithmConfig2 settings;
    if (const auto* genetic_config =
            dynamic_cast<const GeneticAlgorithmConfig2*>(&config)) {
        settings = *genetic_config;
    }

    if (settings.population_size == 0) {
        throw std::invalid_argument(
            "GeneticAlgorithm2 population size must be greater than zero");
    }
    if (!std::isfinite(settings.mutation_rate) || settings.mutation_rate < 0.0 ||
        settings.mutation_rate > 1.0) {
        throw std::invalid_argument("GeneticAlgorithm2 mutation rate must be in [0, 1]");
    }
    if (!std::isfinite(settings.crossover_rate) || settings.crossover_rate < 0.0 ||
        settings.crossover_rate > 1.0) {
        throw std::invalid_argument("GeneticAlgorithm2 crossover rate must be in [0, 1]");
    }

    const std::size_t dimension = problem.dimension();
    SolverResult result;
    result.algorithm = name();
    result.tour.reserve(dimension);

    if (dimension == 0) {
        result.cost = problem.tourCost(result.tour);
        const auto finished = std::chrono::steady_clock::now();
        result.time_ms =
            std::chrono::duration<double, std::milli>(finished - started).count();
        return result;
    }
    if (dimension == 1) {
        result.tour.push_back(0);
        result.cost = problem.tourCost(result.tour);
        const auto finished = std::chrono::steady_clock::now();
        result.time_ms =
            std::chrono::duration<double, std::milli>(finished - started).count();
        return result;
    }

    if (settings.elitism_count > settings.population_size) {
        settings.elitism_count = settings.population_size;
    }
    if (settings.tournament_size == 0) settings.tournament_size = 1;
    settings.tournament_size =
        std::min(settings.tournament_size, settings.population_size);

    std::mt19937_64 random;
    if (settings.seed == 0) {
        std::random_device device;
        random.seed((static_cast<std::uint64_t>(device()) << 32) ^ device());
    } else {
        random.seed(settings.seed);
    }

    // ---- initial population -------------------------------------------------
    Tour base(dimension);
    std::iota(base.begin(), base.end(), 0);

    std::vector<Individual> population;
    population.reserve(settings.population_size);

    const auto push_tour = [&](Tour tour) {
        population.push_back(make_individual(problem, std::move(tour)));
    };

    switch (settings.init_mode) {
    case GeneticInitMode::RANDOM: {
        push_tour(base);
        for (std::size_t i = 1; i < settings.population_size; ++i) {
            Tour tour = base;
            std::shuffle(tour.begin(), tour.end(), random);
            push_tour(std::move(tour));
        }
        break;
    }
    case GeneticInitMode::GREEDY: {
        // One greedy seed plus a 2-opt-improved version of it.
        Tour seed = greedyTour(problem, 0);
        Tour seed_opt = seed;
        twoOptImprove(problem, seed_opt);
        push_tour(std::move(seed));
        push_tour(std::move(seed_opt));
        for (std::size_t i = population.size(); i < settings.population_size; ++i) {
            Tour tour = base;
            std::shuffle(tour.begin(), tour.end(), random);
            push_tour(std::move(tour));
        }
        break;
    }
    case GeneticInitMode::RGREEDY: {
        // 1 2-opt(greedy) seed + ~15% randomized-greedy members + randoms.
        Tour seed = greedyTour(problem, 0);
        Tour seed_opt = seed;
        twoOptImprove(problem, seed_opt);
        push_tour(std::move(seed_opt));

        // SỬA ĐỔI: Giảm tỷ lệ heuristic xuống khoảng 15% (15/100) để giữ đa dạng di truyền
        // Dùng std::max để đảm bảo luôn có ít nhất 1 cá thể nếu quần thể quá nhỏ
        const std::size_t num_heuristic = std::max<std::size_t>(1, settings.population_size * 15 / 100); 
        
        std::uniform_int_distribution<std::size_t> start_city(0, dimension - 1);
        
        // Duyệt thêm các cá thể randomized greedy cho đến khi đạt mốc num_heuristic
        for (std::size_t i = 1; i < num_heuristic && i < settings.population_size; ++i) {
            push_tour(randomizedGreedyTour(problem, start_city(random), 5, random));
        }
        
        // Phần còn lại (85%) là ngẫu nhiên hoàn toàn để tăng không gian tìm kiếm
        for (std::size_t i = population.size(); i < settings.population_size; ++i) {
            Tour tour = base;
            std::shuffle(tour.begin(), tour.end(), random);
            push_tour(std::move(tour));
        }
        break;
    }
    }

    Individual best_ever = population.front();
    for (const auto& ind : population) {
        if (better(ind, best_ever)) best_ever = ind;
    }

    if (settings.best_history) {
        settings.best_history->push_back(best_ever.cost); // generation 0 baseline
    }

    // ---- generational loop ---------------------------------------------------
    std::size_t generations_without_improvement = 0;
    for (std::size_t generation = 0; generation < settings.generations; ++generation) {
        std::sort(population.begin(), population.end(), better);

        // Selection machinery is built once per generation.
        const bool use_tournament =
            settings.selection_type == GeneticSelectionType::TOURNAMENT;
        std::unique_ptr<RouletteSelector> roulette;
        std::unique_ptr<RankSelector> rank;
        std::unique_ptr<RoundRobinSelector> round_robin;
        switch (settings.selection_type) {
        case GeneticSelectionType::TOURNAMENT:
            break;
        case GeneticSelectionType::ROULETTE:
            roulette = std::make_unique<RouletteSelector>(population);
            break;
        case GeneticSelectionType::RANK:
            rank = std::make_unique<RankSelector>(population);
            break;
        case GeneticSelectionType::ROUNDROBIN:
            round_robin = std::make_unique<RoundRobinSelector>(
                population, settings.tournament_size, random);
            break;
        }

        const auto pick_parent = [&](std::mt19937_64& rng) -> const Individual& {
            switch (settings.selection_type) {
            case GeneticSelectionType::TOURNAMENT:
                return tournament_select(population, settings.tournament_size, rng);
            case GeneticSelectionType::ROULETTE:
                return population[roulette->pick(rng)];
            case GeneticSelectionType::RANK:
                return population[rank->pick(rng)];
            case GeneticSelectionType::ROUNDROBIN:
                return population[round_robin->pick(rng)];
            }
            return tournament_select(population, settings.tournament_size, rng);
        };

        std::vector<Individual> next;
        next.reserve(settings.population_size);
        for (std::size_t i = 0; i < settings.elitism_count; ++i) {
            next.push_back(population[i]);
        }

        std::bernoulli_distribution should_crossover(settings.crossover_rate);
        std::bernoulli_distribution should_two_opt(
            settings.two_opt_frequency == 0 ? 0.0
            : 1.0 / static_cast<double>(settings.two_opt_frequency));
        std::bernoulli_distribution should_mutate(settings.mutation_rate);

        while (next.size() < settings.population_size) {
            const Individual& first = pick_parent(random);
            const Individual& second = pick_parent(random);
            Tour child = should_crossover(random)
                ? crossover(first.tour, second.tour, settings.crossover_type, random)
                : first.tour;
            if (should_mutate(random)) {
                apply_mutation(child, settings.mutation_type, random);
                if (should_two_opt(random)) {
                    twoOptImprove(problem, child);
                }
            }
            next.push_back(make_individual(problem, std::move(child)));
        }
        population = std::move(next);

        // Track the best tour ever seen.
        bool improved = false;
        for (const auto& ind : population) {
            if (better(ind, best_ever)) {
                best_ever = ind;
                improved = true;
            }
        }
        if (improved) {
            generations_without_improvement = 0;
        } else if (settings.early_stop_generations > 0) {
            ++generations_without_improvement;
            if (generations_without_improvement >= settings.early_stop_generations) {
                break;
            }
        }

        if (settings.best_history && generation % settings.history_interval == 0) {
            settings.best_history->push_back(best_ever.cost);
        }
    }
    if (settings.best_history) {
        settings.best_history->push_back(best_ever.cost); // always record the end
    }

    result.tour = best_ever.tour;
    result.cost = best_ever.cost;
    const auto finished = std::chrono::steady_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

std::string GeneticAlgorithm2::name() const
{
    return "Genetic Algorithm 2";
}

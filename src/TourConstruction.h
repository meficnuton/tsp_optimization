#pragma once

// TourConstruction.h
//
// Tour construction / local-search helpers shared by the genetic solver and
// the benchmark harnesses:
//
//   * greedyTour            - nearest-neighbour construction from a start city
//   * randomizedGreedyTour  - pick uniformly among the `candidate_count`
//                             nearest unvisited cities (1 = pure greedy)
//   * twoOptImprove         - full first-improvement 2-opt on a tour
//                             (repeated sweeps until no improving move exists)

#include "TSPProblem.h"

#include <cstddef>
#include <random>
#include <vector>

// Nearest-neighbour tour starting at `start`.
std::vector<int> greedyTour(const TSPProblem& problem, std::size_t start);

// Randomized nearest-neighbour tour: at every step the next city is chosen
// uniformly among the `candidate_count` nearest unvisited cities
// (candidate_count == 1 reproduces greedyTour exactly).
std::vector<int> randomizedGreedyTour(
    const TSPProblem& problem,
    std::size_t start,
    std::size_t candidate_count,
    std::mt19937_64& random
);

// Improves `tour` in place with 2-opt until no improving 2-edge exchange
// exists. Returns the total cost improvement (>= 0).
double twoOptImprove(const TSPProblem& problem, std::vector<int>& tour);

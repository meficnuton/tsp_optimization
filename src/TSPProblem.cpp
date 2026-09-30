#include "TSPProblem.h"

#include "MatrixLoader.h"

#include <cassert>
TSPProblem::TSPProblem(
        std::string name,
        std::string comment,
        std::vector<std::vector<double>> distance_matrix
    ) :
    name_(name), comment_(comment), distance_matrix_(std::move(distance_matrix))
    {}

TSPProblem TSPProblem::fromTspFile(const std::string& filename)
{
    MatrixLoader loader(filename);
    return TSPProblem(loader.name(), loader.comment(), loader.distanceMatrix());
}

double TSPProblem::distance(std::size_t from, std::size_t to) const 
{
    assert(from < distance_matrix_.size());
    assert(to < distance_matrix_.size());
    return distance_matrix_[from][to];
}

const std::string& TSPProblem::name() const noexcept
{
    return name_;
}
const std::string& TSPProblem::comment() const noexcept
{
    return comment_;
}
std::size_t TSPProblem::dimension() const noexcept
{
    return distance_matrix_.size();
}
const std::vector<std::vector<double>>& TSPProblem::distanceMatrix() const noexcept
{
    return distance_matrix_;
}

double TSPProblem::tourCost(const std::vector<int>& tour) const
{
    double ret = 0.0;
    size_t k = tour.size();
    for (size_t i = 0; i < k; ++i)
        ret += distance_matrix_[tour[i]][tour[(i + 1) % k]];
    return ret;
}

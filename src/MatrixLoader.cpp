#include "MatrixLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr double PI = 3.141592;
constexpr double GEO_RADIUS = 6378.388;

} // namespace


MatrixLoader::MatrixLoader(
    const std::string& filename
) {
    parse(filename);
}


// ============================================================
// Public interface
// ============================================================

const std::string& MatrixLoader::name() const noexcept {
    return name_;
}

const std::string& MatrixLoader::comment() const noexcept {
    return comment_;
}

int MatrixLoader::dimension() const noexcept {
    return dimension_;
}

const MatrixLoader::Matrix&
MatrixLoader::distanceMatrix() const noexcept {
    return distanceMatrix_;
}


// ============================================================
// Parsing
// ============================================================

void MatrixLoader::parse(
    const std::string& filename
) {
    std::ifstream file(filename);

    if (!file) {
        throw std::runtime_error(
            "Cannot open TSPLIB file: " + filename
        );
    }

    std::string line;

    bool readingCoordinates = false;
    bool readingWeights = false;

    std::vector<double> weights;

    while (std::getline(file, line)) {

        // Handle Windows CRLF
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        line = trim(line);

        if (line.empty()) {
            continue;
        }

        if (line == "EOF") {
            break;
        }

        // ----------------------------------------------------
        // Sections
        // ----------------------------------------------------

        if (line == "NODE_COORD_SECTION") {
            readingCoordinates = true;
            readingWeights = false;
            continue;
        }

        if (line == "EDGE_WEIGHT_SECTION") {
            readingCoordinates = false;
            readingWeights = true;
            continue;
        }

        // ----------------------------------------------------
        // Coordinate section
        // ----------------------------------------------------

        if (readingCoordinates) {

            std::stringstream ss(line);

            int id;
            Point point;

            if (!(ss >> id >> point.x >> point.y)) {
                throw std::runtime_error(
                    "Invalid NODE_COORD_SECTION line: " +
                    line
                );
            }

            /*
             * 2D:
             *
             *     id x y
             *
             * 3D:
             *
             *     id x y z
             */
            ss >> point.z;

            coordinates_.push_back(point);

            continue;
        }

        // ----------------------------------------------------
        // Explicit edge weights
        // ----------------------------------------------------

        if (readingWeights) {

            std::stringstream ss(line);

            double value;

            while (ss >> value) {
                weights.push_back(value);
            }

            continue;
        }

        // ----------------------------------------------------
        // Header
        // ----------------------------------------------------

        const auto colon = line.find(':');

        if (colon == std::string::npos) {
            continue;
        }

        std::string key =
            trim(line.substr(0, colon));

        std::string value =
            trim(line.substr(colon + 1));

        if (key == "NAME") {
            name_ = value;
        }
        else if (key == "COMMENT") {
            comment_ = value;
        }
        else if (key == "TYPE") {
            type_ = value;
            const auto annotation = type_.find(' ');
            if (annotation != std::string::npos) {
                type_ = trim(type_.substr(0, annotation));
            }
        }
        else if (key == "DIMENSION") {
            dimension_ = std::stoi(value);
        }
        else if (key == "EDGE_WEIGHT_TYPE") {
            edgeWeightType_ = value;
        }
        else if (key == "EDGE_WEIGHT_FORMAT") {
            edgeWeightFormat_ = value;
        }
    }

    // --------------------------------------------------------
    // Validation
    // --------------------------------------------------------

    if (type_ != "TSP") {
        throw std::runtime_error(
            "Only TYPE: TSP is supported, got: " + type_
        );
    }

    if (dimension_ <= 0) {
        throw std::runtime_error(
            "Invalid DIMENSION"
        );
    }

    if (edgeWeightType_.empty()) {
        throw std::runtime_error(
            "Missing EDGE_WEIGHT_TYPE"
        );
    }

    if (edgeWeightType_ == "EXPLICIT") {

        if (edgeWeightFormat_.empty()) {
            throw std::runtime_error(
                "EXPLICIT requires EDGE_WEIGHT_FORMAT"
            );
        }

        buildExplicitMatrix(weights);
    }
    else {
        if (
            coordinates_.size() !=
            static_cast<size_t>(dimension_)
        ) {
            throw std::runtime_error(
                "Number of coordinates (" +
                std::to_string(coordinates_.size()) +
                ") does not match DIMENSION (" +
                std::to_string(dimension_) +
                ")"
            );
        }

        buildCoordinateMatrix();
    }
}


// ============================================================
// Distance matrix dispatch
// ============================================================

void MatrixLoader::buildDistanceMatrix() {
    buildCoordinateMatrix();
}


void MatrixLoader::buildCoordinateMatrix() {

    distanceMatrix_.assign(
        dimension_,
        std::vector<double>(
            dimension_,
            0.0
        )
    );

    for (int i = 0; i < dimension_; ++i) {

        for (int j = i + 1;
             j < dimension_;
             ++j) {

            double d = 0.0;

            const Point& a = coordinates_[i];
            const Point& b = coordinates_[j];

            if (edgeWeightType_ == "EUC_2D") {
                d = euclidean2D(a, b);
            }
            else if (edgeWeightType_ == "EUC_3D") {
                d = euclidean3D(a, b);
            }
            else if (edgeWeightType_ == "MAX_2D") {
                d = maximum2D(a, b);
            }
            else if (edgeWeightType_ == "MAX_3D") {
                d = maximum3D(a, b);
            }
            else if (edgeWeightType_ == "MAN_2D") {
                d = manhattan2D(a, b);
            }
            else if (edgeWeightType_ == "MAN_3D") {
                d = manhattan3D(a, b);
            }
            else if (edgeWeightType_ == "CEIL_2D") {
                d = ceil2D(a, b);
            }
            else if (edgeWeightType_ == "GEO") {
                d = geographic(a, b);
            }
            else if (edgeWeightType_ == "ATT") {
                d = pseudoEuclidean(a, b);
            }
            else {
                throw std::runtime_error(
                    "Unsupported EDGE_WEIGHT_TYPE: " +
                    edgeWeightType_
                );
            }

            distanceMatrix_[i][j] = d;
            distanceMatrix_[j][i] = d;
        }
    }
}


// ============================================================
// EUC_2D
// ============================================================

double MatrixLoader::euclidean2D(
    const Point& a,
    const Point& b
) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;

    return tsplibRound(
        std::sqrt(
            dx * dx +
            dy * dy
        )
    );
}


// ============================================================
// EUC_3D
// ============================================================

double MatrixLoader::euclidean3D(
    const Point& a,
    const Point& b
) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;

    return tsplibRound(
        std::sqrt(
            dx * dx +
            dy * dy +
            dz * dz
        )
    );
}


// ============================================================
// MAN_2D
// ============================================================

double MatrixLoader::manhattan2D(
    const Point& a,
    const Point& b
) {
    return tsplibRound(
        std::abs(a.x - b.x) +
        std::abs(a.y - b.y)
    );
}


// ============================================================
// MAN_3D
// ============================================================

double MatrixLoader::manhattan3D(
    const Point& a,
    const Point& b
) {
    return tsplibRound(
        std::abs(a.x - b.x) +
        std::abs(a.y - b.y) +
        std::abs(a.z - b.z)
    );
}


// ============================================================
// MAX_2D
// ============================================================

double MatrixLoader::maximum2D(
    const Point& a,
    const Point& b
) {
    return tsplibRound(
        std::max(
            std::abs(a.x - b.x),
            std::abs(a.y - b.y)
        )
    );
}


// ============================================================
// MAX_3D
// ============================================================

double MatrixLoader::maximum3D(
    const Point& a,
    const Point& b
) {
    return tsplibRound(
        std::max({
            std::abs(a.x - b.x),
            std::abs(a.y - b.y),
            std::abs(a.z - b.z)
        })
    );
}


// ============================================================
// CEIL_2D
// ============================================================

double MatrixLoader::ceil2D(
    const Point& a,
    const Point& b
) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;

    return std::ceil(
        std::sqrt(
            dx * dx +
            dy * dy
        )
    );
}


// ============================================================
// GEO
// ============================================================

double MatrixLoader::geographic(
    const Point& a,
    const Point& b
) {
    /*
     * TSPLIB GEO coordinates:
     *
     * x = degrees + minutes / 60
     *
     * TSPLIB conversion:
     *
     * PI * (degrees + 5 * minutes / 3) / 180
     */

    auto toRadians = [](double value) {

        const int degrees =
            static_cast<int>(value);

        const double minutes =
            value - degrees;

        return PI *
               (
                   degrees +
                   5.0 * minutes / 3.0
               )
               / 180.0;
    };

    const double latitude1 =
        toRadians(a.x);

    const double longitude1 =
        toRadians(a.y);

    const double latitude2 =
        toRadians(b.x);

    const double longitude2 =
        toRadians(b.y);

    const double q1 =
        std::cos(
            longitude1 -
            longitude2
        );

    const double q2 =
        std::cos(
            latitude1 -
            latitude2
        );

    const double q3 =
        std::cos(
            latitude1 +
            latitude2
        );

    const double value =
        GEO_RADIUS *
        std::acos(
            0.5 *
            (
                (1.0 + q1) * q2 -
                (1.0 - q1) * q3
            )
        );

    /*
     * TSPLIB GEO:
     *
     * floor(d + 1)
     */

    return std::floor(value + 1.0);
}


// ============================================================
// ATT
// ============================================================

double MatrixLoader::pseudoEuclidean(
    const Point& a,
    const Point& b
) {
    const double dx =
        a.x - b.x;

    const double dy =
        a.y - b.y;

    const double rij =
        std::sqrt(
            (dx * dx + dy * dy) /
            10.0
        );

    const double tij =
        std::floor(rij);

    if (tij < rij) {
        return tij + 1.0;
    }

    return tij;
}


// ============================================================
// EXPLICIT
// ============================================================

void MatrixLoader::buildExplicitMatrix(
    const std::vector<double>& weights
) {
    distanceMatrix_.assign(
        dimension_,
        std::vector<double>(
            dimension_,
            0.0
        )
    );

    size_t k = 0;

    auto nextValue = [&]() -> double {

        if (k >= weights.size()) {
            throw std::runtime_error(
                "Not enough values in "
                "EDGE_WEIGHT_SECTION"
            );
        }

        return weights[k++];
    };


    // --------------------------------------------------------
    // FULL_MATRIX
    // --------------------------------------------------------

    if (edgeWeightFormat_ == "FULL_MATRIX") {

        for (int i = 0; i < dimension_; ++i) {

            for (int j = 0;
                 j < dimension_;
                 ++j) {

                distanceMatrix_[i][j] =
                    nextValue();
            }
        }

    }


    // --------------------------------------------------------
    // UPPER_ROW
    // --------------------------------------------------------

    else if (edgeWeightFormat_ == "UPPER_ROW") {

        for (int i = 0; i < dimension_; ++i) {

            for (int j = i + 1;
                 j < dimension_;
                 ++j) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }


    // --------------------------------------------------------
    // LOWER_ROW
    // --------------------------------------------------------

    else if (edgeWeightFormat_ == "LOWER_ROW") {

        for (int i = 0; i < dimension_; ++i) {

            for (int j = 0;
                 j < i;
                 ++j) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }


    // --------------------------------------------------------
    // UPPER_DIAG_ROW
    // --------------------------------------------------------

    else if (
        edgeWeightFormat_ ==
        "UPPER_DIAG_ROW"
    ) {

        for (int i = 0; i < dimension_; ++i) {

            for (int j = i;
                 j < dimension_;
                 ++j) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }


    // --------------------------------------------------------
    // LOWER_DIAG_ROW
    // --------------------------------------------------------

    else if (
        edgeWeightFormat_ ==
        "LOWER_DIAG_ROW"
    ) {

        for (int i = 0; i < dimension_; ++i) {

            for (int j = 0;
                 j <= i;
                 ++j) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }


    // --------------------------------------------------------
    // UPPER_COL
    // --------------------------------------------------------

    else if (
        edgeWeightFormat_ ==
        "UPPER_COL"
    ) {

        for (int j = 1;
             j < dimension_;
             ++j) {

            for (int i = 0;
                 i < j;
                 ++i) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }


    // --------------------------------------------------------
    // LOWER_COL
    // --------------------------------------------------------

    else if (
        edgeWeightFormat_ ==
        "LOWER_COL"
    ) {

        for (int j = 0;
             j < dimension_ - 1;
             ++j) {

            for (int i = j + 1;
                 i < dimension_;
                 ++i) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }


    // --------------------------------------------------------
    // UPPER_DIAG_COL
    // --------------------------------------------------------

    else if (
        edgeWeightFormat_ ==
        "UPPER_DIAG_COL"
    ) {

        for (int j = 0;
             j < dimension_;
             ++j) {

            for (int i = 0;
                 i <= j;
                 ++i) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }


    // --------------------------------------------------------
    // LOWER_DIAG_COL
    // --------------------------------------------------------

    else if (
        edgeWeightFormat_ ==
        "LOWER_DIAG_COL"
    ) {

        for (int j = 0;
             j < dimension_;
             ++j) {

            for (int i = j;
                 i < dimension_;
                 ++i) {

                const double d =
                    nextValue();

                distanceMatrix_[i][j] = d;
                distanceMatrix_[j][i] = d;
            }
        }
    }

    else {
        throw std::runtime_error(
            "Unsupported EDGE_WEIGHT_FORMAT: " +
            edgeWeightFormat_
        );
    }
}


// ============================================================
// Utilities
// ============================================================

double MatrixLoader::tsplibRound(
    double x
) {
    /*
     * TSPLIB's nint:
     *
     * floor(x + 0.5)
     */

    return std::floor(x + 0.5);
}


std::string MatrixLoader::trim(
    const std::string& s
) {
    const auto begin =
        s.find_first_not_of(" \t\r\n");

    if (begin == std::string::npos) {
        return "";
    }

    const auto end =
        s.find_last_not_of(" \t\r\n");

    return s.substr(
        begin,
        end - begin + 1
    );
}


std::string MatrixLoader::valueAfterColon(
    const std::string& line
) {
    const auto colon =
        line.find(':');

    if (colon == std::string::npos) {
        return "";
    }

    return trim(
        line.substr(colon + 1)
    );
}

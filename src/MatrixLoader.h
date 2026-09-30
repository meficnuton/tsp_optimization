#pragma once

#include <string>
#include <vector>

class MatrixLoader {
public:
    using Matrix = std::vector<std::vector<double>>;

    explicit MatrixLoader(const std::string& filename);

    const std::string& name() const noexcept;
    const std::string& comment() const noexcept;
    int dimension() const noexcept;

    const Matrix& distanceMatrix() const noexcept;

private:
    struct Point {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    std::string name_;
    std::string comment_;
    int dimension_ = 0;

    std::string type_;
    std::string edgeWeightType_;
    std::string edgeWeightFormat_;

    std::vector<Point> coordinates_;
    Matrix distanceMatrix_;

    void parse(const std::string& filename);

    void buildDistanceMatrix();

    void buildCoordinateMatrix();
    void buildExplicitMatrix(
        const std::vector<double>& weights
    );

    static double euclidean2D(
        const Point& a,
        const Point& b
    );

    static double euclidean3D(
        const Point& a,
        const Point& b
    );

    static double manhattan2D(
        const Point& a,
        const Point& b
    );

    static double manhattan3D(
        const Point& a,
        const Point& b
    );

    static double maximum2D(
        const Point& a,
        const Point& b
    );

    static double maximum3D(
        const Point& a,
        const Point& b
    );

    static double ceil2D(
        const Point& a,
        const Point& b
    );

    static double geographic(
        const Point& a,
        const Point& b
    );

    static double pseudoEuclidean(
        const Point& a,
        const Point& b
    );

    static double tsplibRound(double x);

    static std::string trim(const std::string& s);

    static std::string valueAfterColon(
        const std::string& line
    );
};
#pragma once

#include <kdbush.hpp>
#include <mapbox/geometry/feature.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mapbox {
namespace supercluster {

struct Options {
    std::uint8_t minZoom = 0;   // min zoom to generate clusters on
    std::uint8_t maxZoom = 16;  // max zoom level to cluster the points on
    std::uint16_t radius = 40;  // cluster radius in pixels
    std::uint16_t extent = 512; // tile extent (radius is calculated relative to it)
};

struct Cluster {
    double x;
    double y;
    std::uint32_t num_points;
    std::size_t id;
    std::uint8_t z;
};

class Supercluster {

public:
    Supercluster(const mapbox::geometry::feature_collection<double> &features_,
                 Options options_ = Options())
        : features(features_), options(options_) {

        std::vector<Cluster> clusters;

        std::size_t i = 0;
        for (auto f : features) {
            const auto &p = f.geometry.get<mapbox::geometry::point<double>>();
            Cluster c = { lngX(p.x), latY(p.y), 1, i++, 255 };
            clusters.push_back(c);
        }

        trees.reserve(options.maxZoom + 1);

        trees.emplace_back(clusters);
    }

    const mapbox::geometry::feature_collection<double> features;
    const Options options;

private:
    std::vector<kdbush::KDBush<Cluster>> trees;

    double lngX(double lng) {
        return lng / 360 + 0.5;
    }

    double latY(double lat) {
        const double sine = std::sin(lat * M_PI / 180);
        const double y = 0.5 - 0.25 * std::log((1 + sine) / (1 - sine)) / M_PI;
        return std::min(std::max(y, 0.0), 1.0);
    }
};

} // namespace supercluster
} // namespace mapbox

template <>
struct kdbush::nth<0, mapbox::supercluster::Cluster> {
    inline static double get(const mapbox::supercluster::Cluster &c) {
        return c.x;
    };
};
template <>
struct kdbush::nth<1, mapbox::supercluster::Cluster> {
    inline static double get(const mapbox::supercluster::Cluster &c) {
        return c.y;
    };
};

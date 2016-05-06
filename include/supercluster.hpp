#pragma once

#include <kdbush.hpp>
#include <mapbox/geometry/feature.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace mapbox {
namespace supercluster {

struct Cluster {
    double x;
    double y;
    std::uint32_t num_points;
    std::size_t id = 0;
    std::uint8_t zoom = 255;
};

} // namespace supercluster
} // namespace mapbox

namespace kdbush {

using Cluster = mapbox::supercluster::Cluster;

template <>
struct nth<0, Cluster> {
    inline static double get(const Cluster &c) {
        return c.x;
    };
};
template <>
struct nth<1, Cluster> {
    inline static double get(const Cluster &c) {
        return c.y;
    };
};

} // namespace kdbush

namespace mapbox {
namespace supercluster {

struct Options {
    std::uint16_t minZoom = 0;  // min zoom to generate clusters on
    std::uint16_t maxZoom = 16; // max zoom level to cluster the points on
    std::uint16_t radius = 40;  // cluster radius in pixels
    std::uint16_t extent = 512; // tile extent (radius is calculated relative to it)
};

class Supercluster {

    using FeatureCollection = mapbox::geometry::feature_collection<double>;
    using Point = mapbox::geometry::point<double>;

public:
    const FeatureCollection features;
    const Options options;

    Supercluster(const FeatureCollection &features_, const Options options_ = Options())
        : features(features_), options(options_) {

        std::vector<Cluster> clusters;

        // generate a cluster object for each point
        std::size_t i = 0;
        for (auto f : features) {
            const auto &p = f.geometry.get<Point>();
            Cluster c = { lngX(p.x), latY(p.y), 1, i++ };
            clusters.push_back(c);
        }

        // cluster points on max zoom, then cluster the results on previous zoom, etc.;
        // results in a cluster hierarchy across zoom levels
        auto &zoomClusters = clusters;

        for (int z = options.maxZoom; z >= options.minZoom; z--) {
            // index input points into a KD-tree
            trees.emplace(z + 1, zoomClusters);

            // create a new set of clusters for the zoom
            zoomClusters = clusterZoom(zoomClusters, z);
        }

        // index top-level clusters
        trees.emplace(options.minZoom, zoomClusters);
    }

private:
    std::unordered_map<std::uint8_t, kdbush::KDBush<Cluster>> trees;

    std::vector<Cluster> clusterZoom(std::vector<Cluster> &points, std::uint8_t zoom) {
        std::vector<Cluster> clusters;

        double r = options.radius / (options.extent * std::pow(2, zoom));

        for (auto &p : points) {
            // if we've already visited the point at this zoom level, skip it
            if (p.zoom <= zoom) continue;
            p.zoom = zoom;

            // find all nearby points
            std::vector<std::size_t> neighborIds;
            trees.find(zoom + 1)->second.within(p.x, p.y, r, std::back_inserter(neighborIds));

            bool foundNeighbors = false;
            auto num_points = p.num_points;
            double wx = p.x * num_points;
            double wy = p.y * num_points;

            for (auto id : neighborIds) {
                auto &b = points[id];
                // filter out neighbors that are too far or already processed
                if (zoom < b.zoom) {
                    foundNeighbors = true;
                    // save the zoom (so it doesn't get processed twice)
                    b.zoom = zoom;
                    // accumulate coordinates for calculating weighted center
                    wx += b.x * b.num_points;
                    wy += b.y * b.num_points;
                    num_points += b.num_points;
                }
            }

            if (foundNeighbors) {
                Cluster c = { wx / num_points, wy / num_points, num_points };
                clusters.push_back(c);
            } else {
                clusters.push_back(p);
            }
        }

        std::cerr << "z" << int(zoom) << ", num clusters: " << clusters.size() << "\n";

        return clusters;
    }

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

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

#ifdef DEBUG_TIMER
class Timer {
public:
    std::chrono::high_resolution_clock::time_point started;
    Timer() {
        started = std::chrono::high_resolution_clock::now();
    }
    void operator()(std::string msg) {
        const auto now = std::chrono::high_resolution_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::microseconds>(now - started);
        std::cerr << msg << ": " << double(ms.count()) / 1000 << "ms\n";
        started = now;
    }
};
#endif

struct Options {
    std::uint8_t minZoom = 0;   // min zoom to generate clusters on
    std::uint8_t maxZoom = 16;  // max zoom level to cluster the points on
    std::uint16_t radius = 40;  // cluster radius in pixels
    std::uint16_t extent = 512; // tile extent (radius is calculated relative to it)
};

class Supercluster {

    using GeoJSONPoint = mapbox::geometry::point<double>;
    using GeoJSONFeatures = mapbox::geometry::feature_collection<double>;

    using TilePoint = mapbox::geometry::point<std::int16_t>;
    using TileFeature = mapbox::geometry::feature<std::int16_t>;
    using TileFeatures = mapbox::geometry::feature_collection<std::int16_t>;

public:
    const GeoJSONFeatures features;
    const Options options;

    Supercluster(const GeoJSONFeatures &features_, const Options options_ = Options())
        : features(features_), options(options_) {

        std::vector<Cluster> clusters;

#ifdef DEBUG_TIMER
        Timer timer;
#endif
        // generate a cluster object for each point
        std::size_t i = 0;
        for (const auto &f : features) {
            const auto &p = f.geometry.get<GeoJSONPoint>();
            Cluster c = { lngX(p.x), latY(p.y), 1, i++ };
            clusters.push_back(c);
        }
#ifdef DEBUG_TIMER
        timer("generate single point clusters");
#endif

        // cluster points on max zoom, then cluster the results on previous zoom, etc.;
        // results in a cluster hierarchy across zoom levels
        auto &zoomClusters = clusters;

        for (int z = options.maxZoom; z >= options.minZoom; z--) {
            // index input points into a KD-tree
            trees.emplace(z + 1, zoomClusters);
            clustersByZoom[z + 1] = zoomClusters;

            // create a new set of clusters for the zoom
            zoomClusters = clusterZoom(zoomClusters, z);
#ifdef DEBUG_TIMER
            timer(std::to_string(zoomClusters.size()) + " clusters");
#endif
        }

        // index top-level clusters
        trees.emplace(options.minZoom, zoomClusters);
        clustersByZoom[options.minZoom] = zoomClusters;
    }

    TileFeatures getTile(std::uint8_t z, std::uint32_t x, std::uint32_t y) {
        double const z2 = std::pow(2, z);
        double const r = options.radius / options.extent;

        std::uint8_t zoom = limitZoom(z);

        TileFeatures result;

        auto const &clusters = clustersByZoom.find(zoom)->second;
        auto const &extent = options.extent;
        auto const &input_features = features;

        trees.find(zoom)->second.range((x - r) / z2, (y - r) / z2, (x + 1 + r) / z2, (y + 1 + r) / z2,
            [&clusters, &extent, &z2, &x, &y, &result, &input_features](auto &id) {
            auto const &c = clusters[id];

            TilePoint point(std::round(extent * (c.x * z2 - x)),
                            std::round(extent * (c.y * z2 - y)));

            TileFeature feature{ point };

            if (c.num_points == 1) {
                feature.properties = input_features[c.id].properties;
            } else {
                feature.properties["cluster"] = true;
                feature.properties["point_count"] = static_cast<std::uint64_t>(c.num_points);
            }

            result.push_back(feature);
        });

        return result;
    }

private:
    std::unordered_map<std::uint8_t, kdbush::KDBush<Cluster>> trees;
    std::unordered_map<std::uint8_t, std::vector<Cluster>> clustersByZoom;

    std::vector<Cluster> clusterZoom(std::vector<Cluster> &points, std::uint8_t zoom) {
        std::vector<Cluster> clusters;

        double const r = options.radius / (options.extent * std::pow(2, zoom));

        for (auto &p : points) {
            // if we've already visited the point at this zoom level, skip it
            if (p.zoom <= zoom) continue;
            p.zoom = zoom;

            auto num_points = p.num_points;
            double wx = p.x * num_points;
            double wy = p.y * num_points;

            // find all nearby points
            trees.find(zoom + 1)->second.within(
                p.x, p.y, r, [&points, &wx, &wy, &num_points, &zoom](const auto &id) {
                    auto &b = points[id];
                    // filter out neighbors that are too far or already processed
                    if (zoom < b.zoom) {
                        // save the zoom (so it doesn't get processed twice)
                        b.zoom = zoom;
                        // accumulate coordinates for calculating weighted center
                        wx += b.x * b.num_points;
                        wy += b.y * b.num_points;
                        num_points += b.num_points;
                    }
                });

            if (num_points != p.num_points) { // found neighbors
                Cluster c = { wx / num_points, wy / num_points, num_points };
                clusters.push_back(c);
            } else {
                clusters.push_back(p);
            }
        }

        return clusters;
    }

    std::uint8_t limitZoom(std::uint8_t z) {
        return std::max(
            static_cast<uint16_t>(options.minZoom),
            std::min(static_cast<uint16_t>(z), static_cast<uint16_t>(options.maxZoom + 1)));
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

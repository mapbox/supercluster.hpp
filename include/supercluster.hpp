#pragma once

#include <kdbush.hpp>
#include <mapbox/geometry/feature.hpp>

#include <algorithm>
#include <cmath>
#ifdef DEBUG_TIMER
#include <chrono>
#endif
#include <cstdint>
#include <iostream>
#include <vector>

namespace mapbox {
namespace supercluster {

struct Cluster {
    double x;
    double y;
    std::uint32_t num_points;
    std::uint32_t id = 0;
    bool visited = false;
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

#ifdef DEBUG_TIMER
        Timer timer;
#endif
        // convert and index initial points
        zooms.emplace(options.maxZoom + 1, features);
#ifdef DEBUG_TIMER
        timer(std::to_string(features.size()) + " initial points");
#endif
        for (int z = options.maxZoom; z >= options.minZoom; z--) {
            // cluster points from the previous zoom level
            const double r = options.radius / (options.extent * std::pow(2, z));
            zooms.emplace(z, Zoom(zooms[z + 1], r));
#ifdef DEBUG_TIMER
            timer(std::to_string(zooms[z].clusters.size()) + " clusters");
#endif
        }
    }

    TileFeatures getTile(std::uint8_t z, std::uint32_t x, std::uint32_t y) {
        double const z2 = std::pow(2, z);
        double const r = options.radius / options.extent;
        auto &zoom = zooms[limitZoom(z)];
        TileFeatures result;

        auto visitor = [&, this](const auto &id) {
            auto const &c = zoom.clusters[id];

            TilePoint point(std::round(this->options.extent * (c.x * z2 - x)),
                            std::round(this->options.extent * (c.y * z2 - y)));
            TileFeature feature{ point };

            if (c.num_points == 1) {
                feature.properties = this->features[c.id].properties;
            } else {
                feature.properties["cluster"] = true;
                feature.properties["point_count"] = static_cast<std::uint64_t>(c.num_points);
            }

            result.push_back(feature);
        };
        zoom.tree.range((x - r) / z2, (y - r) / z2, (x + 1 + r) / z2, (y + 1 + r) / z2, visitor);

        return result;
    }

private:
    struct Zoom {
        kdbush::KDBush<Cluster, std::uint32_t> tree;
        std::vector<Cluster> clusters;

        Zoom() = default;

        Zoom(const GeoJSONFeatures &features) {
            // generate a cluster object for each point
            std::uint32_t i = 0;

            for (const auto &f : features) {
                const auto &p = f.geometry.get<GeoJSONPoint>();
                clusters.push_back({ lngX(p.x), latY(p.y), 1, i++ });
            }

            tree.fill(clusters);
        }

        Zoom(Zoom &previous, double r) {
            for (auto &p : previous.clusters) {
                if (p.visited) continue;
                p.visited = true;

                auto num_points = p.num_points;
                double wx = p.x * num_points;
                double wy = p.y * num_points;

                // find all nearby points
                previous.tree.within(p.x, p.y, r, [&](const auto &id) {
                    auto &b = previous.clusters[id];

                    // filter out neighbors that are already processed
                    if (b.visited) return;
                    b.visited = true;

                    // accumulate coordinates for calculating weighted center
                    wx += b.x * b.num_points;
                    wy += b.y * b.num_points;
                    num_points += b.num_points;
                });

                clusters.push_back({ wx / num_points, wy / num_points, num_points, p.id });
            }

            tree.fill(clusters);
        }
    };

    std::unordered_map<std::uint8_t, Zoom> zooms;

    std::uint8_t limitZoom(std::uint8_t z) {
        if (z < options.minZoom) return options.minZoom;
        if (z > options.maxZoom + 1) return options.maxZoom + 1;
        return z;
    }

    static double lngX(double lng) {
        return lng / 360 + 0.5;
    }

    static double latY(double lat) {
        const double sine = std::sin(lat * M_PI / 180);
        const double y = 0.5 - 0.25 * std::log((1 + sine) / (1 - sine)) / M_PI;
        return std::min(std::max(y, 0.0), 1.0);
    }
};

} // namespace supercluster
} // namespace mapbox

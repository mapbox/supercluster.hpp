#pragma once

#include <kdbush.hpp>
#include <mapbox/geometry/feature.hpp>
#include <mapbox/geometry/point_arithmetic.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

#ifdef DEBUG_TIMER
#include <chrono>
#include <iostream>
#endif

namespace mapbox {
namespace supercluster {

using namespace mapbox::geometry;

struct Cluster {
    const point<double> pos;
    const std::uint32_t num_points;
    std::uint32_t id;
    std::uint32_t parent_id = 0;
    bool visited = false;

    Cluster(const point<double> pos_, const std::uint32_t num_points_, const std::uint32_t id_)
        : pos(pos_), num_points(num_points_), id(id_) {
    }

    feature<double> toGeoJSON() const {
        const double x = (pos.x - 0.5) * 360.0;
        const double y =
            360.0 * std::atan(std::exp((180.0 - pos.y * 360.0) * M_PI / 180)) / M_PI - 90.0;
        return { point<double>{ x, y }, getProperties(),
                 std::experimental::make_optional(identifier(static_cast<std::uint64_t>(id))) };
    }

    property_map getProperties() const {
        property_map properties{};
        properties["cluster"] = true;
        properties["cluster_id"] = static_cast<std::uint64_t>(id);
        properties["point_count"] = static_cast<std::uint64_t>(num_points);

        std::stringstream ss;
        if (num_points >= 1000) {
            ss << std::fixed;
            if (num_points < 10000) {
                ss << std::setprecision(1);
            }
            ss << double(num_points) / 1000 << "k";
        } else {
            ss << num_points;
        }
        properties.emplace("point_count_abbreviated", ss.str());

        return properties;
    }
};

} // namespace supercluster
} // namespace mapbox

namespace kdbush {

using Cluster = mapbox::supercluster::Cluster;

template <>
struct nth<0, Cluster> {
    inline static double get(const Cluster &c) {
        return c.pos.x;
    };
};
template <>
struct nth<1, Cluster> {
    inline static double get(const Cluster &c) {
        return c.pos.y;
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
    using GeoJSONPoint = point<double>;
    using GeoJSONFeature = feature<double>;
    using GeoJSONFeatures = feature_collection<double>;

    using TilePoint = point<std::int16_t>;
    using TileFeature = feature<std::int16_t>;
    using TileFeatures = feature_collection<std::int16_t>;

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
            zooms.emplace(z, Zoom(zooms[z + 1], r, z));
#ifdef DEBUG_TIMER
            timer(std::to_string(zooms[z].clusters.size()) + " clusters");
#endif
        }
    }

    TileFeatures getTile(const std::uint8_t z, const std::uint32_t x_, const std::uint32_t y) {
        TileFeatures result;
        auto &zoom = zooms[limitZoom(z)];

        const std::uint32_t z2 = std::pow(2, z);
        const double r = static_cast<double>(options.radius) / options.extent;
        std::int32_t x = x_;

        const auto visitor = [&, this](const auto &id) {
            const auto &c = zoom.clusters[id];

            const TilePoint point(::round(this->options.extent * (c.pos.x * z2 - x)),
                                  ::round(this->options.extent * (c.pos.y * z2 - y)));

            if (c.num_points == 1) {
                const auto &original_feature = this->features[c.id];
                result.emplace_back(point, original_feature.properties, original_feature.id);
            } else {
                result.emplace_back(
                    point, c.getProperties(),
                    std::experimental::make_optional(identifier(static_cast<std::uint64_t>(c.id))));
            }
        };

        const double top = (y - r) / z2;
        const double bottom = (y + 1 + r) / z2;

        zoom.tree.range((x - r) / z2, top, (x + 1 + r) / z2, bottom, visitor);

        if (x_ == 0) {
            x = z2;
            zoom.tree.range(1 - r / z2, top, 1, bottom, visitor);
        }
        if (x_ == z2 - 1) {
            x = -1;
            zoom.tree.range(0, top, r / z2, bottom, visitor);
        }

        return result;
    }

    GeoJSONFeatures getChildren(const std::uint32_t cluster_id) {
        GeoJSONFeatures children;
        eachChild(cluster_id,
                  [&, this](const auto &c) { children.push_back(this->clusterToGeoJSON(c)); });
        return children;
    }

    GeoJSONFeatures getLeaves(const std::uint32_t cluster_id,
                              const std::uint32_t limit = 10,
                              const std::uint32_t offset = 0) {
        GeoJSONFeatures leaves;
        std::uint32_t skipped = 0;
        std::uint32_t limit_ = limit;
        eachLeaf(cluster_id, limit_, offset, skipped,
                 [&, this](const auto &c) { leaves.push_back(this->clusterToGeoJSON(c)); });
        return leaves;
    }

    std::uint8_t getClusterExpansionZoom(std::uint32_t cluster_id) {
        auto cluster_zoom = (cluster_id % 32) - 1;
        while (cluster_zoom < options.maxZoom) {
            std::uint32_t num_children = 0;

            eachChild(cluster_id, [&](const auto &c) {
                num_children++;
                cluster_id = c.id;
            });

            cluster_zoom++;

            if (num_children != 1)
                break;
        }
        return cluster_zoom;
    }

private:
    struct Zoom {
        kdbush::KDBush<Cluster, std::uint32_t> tree;
        std::vector<Cluster> clusters;

        Zoom() = default;

        Zoom(const GeoJSONFeatures &features_) {
            // generate a cluster object for each point
            std::uint32_t i = 0;

            for (const auto &f : features_) {
                clusters.emplace_back(project(f.geometry.get<GeoJSONPoint>()), 1, i++);
            }

            tree.fill(clusters);
        }

        Zoom(Zoom &previous, const double r, const std::uint8_t zoom) {
            for (std::size_t i = 0; i < previous.clusters.size(); i++) {
                auto &p = previous.clusters[i];

                if (p.visited)
                    continue;
                p.visited = true;

                auto num_points = p.num_points;
                point<double> weight = p.pos * double(num_points);

                const std::uint32_t id = (i << 5) + (zoom + 1);

                // find all nearby points
                previous.tree.within(p.pos.x, p.pos.y, r, [&](const auto &neighbor_id) {
                    auto &b = previous.clusters[neighbor_id];

                    // filter out neighbors that are already processed
                    if (b.visited)
                        return;
                    b.visited = true;
                    b.parent_id = id;

                    // accumulate coordinates for calculating weighted center
                    weight += b.pos * double(b.num_points);
                    num_points += b.num_points;
                });

                if (num_points == 1) {
                    clusters.emplace_back(weight, 1, p.id);
                } else {
                    p.parent_id = id;
                    clusters.emplace_back(weight / double(num_points), num_points, id);
                }
            }

            tree.fill(clusters);
        }
    };

    std::unordered_map<std::uint8_t, Zoom> zooms;

    std::uint8_t limitZoom(const std::uint8_t z) {
        if (z < options.minZoom)
            return options.minZoom;
        if (z > options.maxZoom + 1)
            return options.maxZoom + 1;
        return z;
    }

    template <typename TVisitor>
    void eachChild(const std::uint32_t cluster_id, const TVisitor &visitor) {
        const auto origin_id = cluster_id >> 5;
        const auto origin_zoom = cluster_id % 32;

        const auto zoom_iter = zooms.find(origin_zoom);
        if (zoom_iter == zooms.end()) {
            throw std::runtime_error("No cluster with the specified id.");
        }

        auto &zoom = zoom_iter->second;
        if (origin_id >= zoom.clusters.size()) {
            throw std::runtime_error("No cluster with the specified id.");
        }

        const double r = options.radius / (double(options.extent) * std::pow(2, origin_zoom - 1));
        const auto &origin = zoom.clusters[origin_id];

        bool hasChildren = false;

        zoom.tree.within(origin.pos.x, origin.pos.y, r, [&](const auto &id) {
            const auto &c = zoom.clusters[id];
            if (c.parent_id == cluster_id) {
                visitor(c);
                hasChildren = true;
            }
        });

        if (!hasChildren) {
            throw std::runtime_error("No cluster with the specified id.");
        }
    }

    template <typename TVisitor>
    void eachLeaf(const std::uint32_t cluster_id,
                  std::uint32_t &limit,
                  const std::uint32_t offset,
                  std::uint32_t &skipped,
                  const TVisitor &visitor) {

        eachChild(cluster_id, [&, this](const auto &c) {
            if (limit == 0)
                return;
            if (c.num_points > 1) {
                if (skipped + c.num_points <= offset) {
                    // skip the whole cluster
                    skipped += c.num_points;
                } else {
                    // enter the cluster
                    this->eachLeaf(c.id, limit, offset, skipped, visitor);
                    // exit the cluster
                }
            } else if (skipped < offset) {
                // skip a single point
                skipped++;
            } else {
                // visit a single point
                visitor(c);
                limit--;
            }
        });
    }

    GeoJSONFeature clusterToGeoJSON(const Cluster &c) {
        return c.num_points == 1 ? features[c.id] : c.toGeoJSON();
    }

    static point<double> project(const GeoJSONPoint &p) {
        const auto lngX = p.x / 360 + 0.5;
        const double sine = std::sin(p.y * M_PI / 180);
        const double y = 0.5 - 0.25 * std::log((1 + sine) / (1 - sine)) / M_PI;
        const auto latY = std::min(std::max(y, 0.0), 1.0);
        return { lngX, latY };
    }
};

} // namespace supercluster
} // namespace mapbox

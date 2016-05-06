#pragma once

#include <mapbox/geometry/feature.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mapbox {

using FeatureCollection = mapbox::geometry::feature_collection<double>;

class Supercluster {
public:
    Supercluster(FeatureCollection features) {
    }

private:
} // namespace mapbox

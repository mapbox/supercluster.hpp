#include <mapbox/geometry/feature.hpp>
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>

#define DEBUG_TIMER true

#include <supercluster.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>

int main() {
    std::FILE *fp = std::fopen("../supercluster/tmp/trees-na2.json", "r");
    char buffer[65536];
    rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));

    mapbox::supercluster::Timer timer;

    rapidjson::Document d;
    d.ParseStream(is);
    timer("parse JSON");

    const auto &json_features = d["features"];

    mapbox::geometry::feature_collection<double> features;
    features.reserve(json_features.Size());

    for (auto itr = json_features.Begin(); itr != json_features.End(); ++itr) {
        const auto &json_coords = (*itr)["geometry"]["coordinates"];
        const auto lng = json_coords[0].GetDouble();
        const auto lat = json_coords[1].GetDouble();
        mapbox::geometry::point<double> point(lng, lat);
        mapbox::geometry::feature<double> feature{ point };
        features.push_back(feature);
    }
    timer("convert to geometry.hpp");

    mapbox::supercluster::Options options;
    options.radius = 75;
    mapbox::supercluster::Supercluster index(features, options);

    timer("total supercluster time");
}

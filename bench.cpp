#include <mapbox/geometry/feature.hpp>
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>

#include <supercluster.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>

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

int main() {
    std::FILE *fp = std::fopen("../supercluster/tmp/trees-na2.json", "r");
    char buffer[65536];
    rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));

    Timer timer;

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

    mapbox::supercluster::Supercluster index(features);

    timer("construct supercluster");
}

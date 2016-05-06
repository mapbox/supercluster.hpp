#include <mapbox/geometry/feature.hpp>
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>

namespace bench {

static std::chrono::time_point<std::chrono::high_resolution_clock> started;

void start() {
    started = std::chrono::high_resolution_clock::now();
}

void report(std::string msg) {
    const auto finished = std::chrono::high_resolution_clock::now();
    std::cerr << msg << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count()
              << "ms\n";
    started = finished;
}

} // namespace bench

int main() {
    std::FILE *fp = std::fopen("../supercluster/tmp/trees-na2.json", "r");
    char readBuffer[65536];
    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));

    bench::start();
    rapidjson::Document d;
    d.ParseStream(is);
    bench::report("parse JSON");

    mapbox::geometry::feature_collection<double> collection;

    const auto &json_features = d["features"];

    for (auto itr = json_features.Begin(); itr != json_features.End(); ++itr) {
        const auto &json_coords = (*itr)["geometry"]["coordinates"];
        const auto lng = json_coords[0].GetDouble();
        const auto lat = json_coords[1].GetDouble();
        mapbox::geometry::point<double> point(lng, lat);
        mapbox::geometry::feature<double> feature{ point };
        collection.push_back(feature);
    }
    bench::report("convert to geometry.hpp");

    return 0;
}

#include <mapbox/feature.hpp>
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>

#include <supercluster.hpp>

#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>

int main() {
    std::FILE *fp = std::fopen("test/fixtures/places.json", "r");
    char buffer[65536];
    rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));

    rapidjson::Document d;
    d.ParseStream(is);

    const auto &json_features = d["features"];

    mapbox::feature::feature_collection<double> features;
    features.reserve(json_features.Size());

    for (auto itr = json_features.Begin(); itr != json_features.End(); ++itr) {
        const auto &json_coords = (*itr)["geometry"]["coordinates"];
        const auto lng = json_coords[0].GetDouble();
        const auto lat = json_coords[1].GetDouble();
        mapbox::geometry::point<double> point(lng, lat);
        mapbox::feature::feature<double> feature{ point };
        feature.properties["name"] = std::string((*itr)["properties"]["name"].GetString());
        features.push_back(feature);
    }

    mapbox::supercluster::Options options;
    mapbox::supercluster::Supercluster index(features, options);

    mapbox::feature::feature_collection<std::int16_t> tile = index.getTile(0, 0, 0);
    assert(tile.size() == 39);

    std::uint64_t num_points = 0;

    for (auto &f : tile) {
        const auto itr = f.properties.find("cluster");
        if (itr != f.properties.end() && itr->second.get<bool>()) {
            num_points += f.properties["point_count"].get<std::uint64_t>();
        } else {
            num_points += 1;
        }
    }

    assert(num_points == 196);

    auto children = index.getChildren(1);

    assert(children.size() == 4);
    assert(children[0].properties["point_count"].get<std::uint64_t>() == 6);
    assert(children[1].properties["point_count"].get<std::uint64_t>() == 7);
    assert(children[2].properties["point_count"].get<std::uint64_t>() == 2);
    assert(children[3].properties["name"].get<std::string>() == "Bermuda Islands");

    assert(index.getClusterExpansionZoom(1) == 1);
    assert(index.getClusterExpansionZoom(33) == 1);
    assert(index.getClusterExpansionZoom(353) == 2);
    assert(index.getClusterExpansionZoom(833) == 2);
    assert(index.getClusterExpansionZoom(1857) == 3);

    auto leaves = index.getLeaves(1, 10, 5);

    assert(leaves[0].properties["name"].get<std::string>() == "Niagara Falls");
    assert(leaves[1].properties["name"].get<std::string>() == "Cape San Blas");
    assert(leaves[2].properties["name"].get<std::string>() == "Cape Sable");
    assert(leaves[3].properties["name"].get<std::string>() == "Cape Canaveral");
    assert(leaves[4].properties["name"].get<std::string>() == "San  Salvador");
    assert(leaves[5].properties["name"].get<std::string>() == "Cabo Gracias a Dios");
    assert(leaves[6].properties["name"].get<std::string>() == "I. de Cozumel");
    assert(leaves[7].properties["name"].get<std::string>() == "Grand Cayman");
    assert(leaves[8].properties["name"].get<std::string>() == "Miquelon");
    assert(leaves[9].properties["name"].get<std::string>() == "Cape Bauld");

    mapbox::supercluster::Options options2;
    options2.radius = 60;
    options2.extent = 256;
    options2.maxZoom = 4;
    mapbox::supercluster::Supercluster index2(features, options2);
    assert(index2.getClusterExpansionZoom(2436) == 5);

}

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
        feature.properties["scalerank"] =
            std::uint64_t((*itr)["properties"]["scalerank"].GetUint64());
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

    auto map =
        [](const mapbox::feature::property_map &properties) -> mapbox::feature::property_map {
        mapbox::feature::property_map ret{};
        auto iter = properties.find("scalerank");
        if (iter != properties.end()) {
            ret["sum"] = iter->second.get<std::uint64_t>();
        }
        return ret;
    };
    auto reduce = [](mapbox::feature::property_map &toUpdate,
                     const mapbox::feature::property_map &toFill) {
        auto iter1 = toUpdate.find("sum");
        auto iter2 = toFill.find("sum");
        if (iter1 != toUpdate.end() && iter2 != toFill.end()) {
            iter1->second.set<std::uint64_t>(iter1->second.get<std::uint64_t>() +
                                             iter2->second.get<std::uint64_t>());
        }
    };
    mapbox::supercluster::Options options3;
    options3.map = map;
    options3.reduce = reduce;

    mapbox::supercluster::Supercluster index3(features, options3);

    mapbox::feature::feature_collection<std::int16_t> tile3 = index3.getTile(0, 0, 0);
    assert(!tile3.empty());
    assert(tile3[0].properties.count("sum") != 0);
    assert(tile3[0].properties["sum"].get<std::uint64_t>() == 69);

    mapbox::supercluster::Options options4;
    options4.radius = 100;
    options4.map = map;
    options4.reduce = reduce;

    mapbox::supercluster::Supercluster index4(features, options4);

    const std::vector<std::uint64_t> expectVec0{ 298, 122, 12, 36,  98, 7,  24,
                                                 8,   125, 98, 125, 12, 36, 8 };
    const std::vector<std::uint64_t> expectVec1{ 146, 84, 63, 23, 34, 12, 19, 29, 8, 8, 80, 35 };
    std::vector<std::uint64_t> actualVec0{}, actualVec1{};
    mapbox::feature::feature_collection<std::int16_t> tile4_0 = index4.getTile(0, 0, 0);
    for (decltype(tile4_0.size()) i = 0; i < tile4_0.size(); ++i) {
        if (tile4_0[i].properties.count("sum") != 0) {
            actualVec0.push_back(tile4_0[i].properties["sum"].get<std::uint64_t>());
        }
    }
    assert(actualVec0 == expectVec0);
    mapbox::feature::feature_collection<std::int16_t> tile4_1 = index4.getTile(1, 0, 0);
    for (decltype(tile4_1.size()) i = 0; i < tile4_1.size(); ++i) {
        if (tile4_1[i].properties.count("sum") != 0) {
            actualVec1.push_back(tile4_1[i].properties["sum"].get<std::uint64_t>());
        }
    }
    assert(actualVec1 == expectVec1);
}

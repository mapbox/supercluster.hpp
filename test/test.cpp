#include <mapbox/feature.hpp>
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>

#include <supercluster.hpp>

#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>

mapbox::feature::feature_collection<double> parseFeatures(const char *filename) {
    std::FILE *fp = std::fopen(filename, "r");
    char buffer[65536];
    rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));

    rapidjson::Document d;
    d.ParseStream(is);

    std::fclose(fp);
    const auto &json_features = d["features"];
    assert(json_features.IsArray());
    mapbox::feature::feature_collection<double> features;
    features.reserve(json_features.Size());

    for (auto itr = json_features.Begin(); itr != json_features.End(); ++itr) {
        const auto &json_coords = (*itr)["geometry"]["coordinates"];
        const auto lng = json_coords[0].GetDouble();
        const auto lat = json_coords[1].GetDouble();
        mapbox::geometry::point<double> point(lng, lat);

        mapbox::feature::feature<double> feature{ point };
        const auto &properties = (*itr)["properties"];
        const auto readProperty = [&feature, &properties](const char *key) {
            if (properties.HasMember(key)) {
                const auto &value = properties[key];
                if (value.IsNull())
                    feature.properties[key] = std::string("null");
                if (value.IsString())
                    feature.properties[key] = std::string(value.GetString());
                if (value.IsUint64())
                    feature.properties[key] = std::uint64_t(value.GetUint64());
                if (value.IsDouble())
                    feature.properties[key] = value.GetDouble();
            }
        };
        readProperty("name");
        readProperty("scalerank");
        readProperty("lat_y");
        readProperty("long_x");
        readProperty("region");
        readProperty("featureclass");
        readProperty("comment");
        readProperty("name_alt");
        readProperty("subregion");
        features.push_back(feature);
    }

    return features;
}

int main() {
    const auto features = parseFeatures("test/fixtures/places.json");

    // ----------------------- test 1 ------------------------------------
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

    // ----------------------- test 2 ------------------------------------
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

    // ----------------------- test 3 ------------------------------------
    mapbox::supercluster::Options options3;
    options3.map = map;
    options3.reduce = reduce;

    mapbox::supercluster::Supercluster index3(features, options3);

    mapbox::feature::feature_collection<std::int16_t> tile3 = index3.getTile(0, 0, 0);
    assert(!tile3.empty());
    assert(tile3[0].properties.count("sum") != 0);
    assert(tile3[0].properties["sum"].get<std::uint64_t>() == 69);

    // ----------------------- test 4 ------------------------------------
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

    // ----------------------- test 5 ------------------------------------
    mapbox::supercluster::Options options5;
    options5.minPoints = 5;
    mapbox::supercluster::Supercluster index5(features, options5);

    mapbox::feature::feature_collection<std::int16_t> tile5 = index5.getTile(0, 0, 0);
    assert(tile5.size() == 49);

    std::uint64_t num_points1 = 0;
    for (auto &f : tile5) {
        const auto itr = f.properties.find("cluster");
        if (itr != f.properties.end() && itr->second.get<bool>()) {
            const auto &point_count = f.properties["point_count"].get<std::uint64_t>();
            assert(point_count >= 5);
            num_points1 += point_count;
        } else {
            num_points1 += 1;
        }
    }

    assert(num_points1 == 195);

    // ----------------------- test 6 - getClusters -----------------------
    mapbox::supercluster::Options options6;
    mapbox::supercluster::Supercluster index6(features, options6);

    double bbox1[4] = {129.426390, -103.720017, -445.930843, 114.518236};
    double bbox2[4] = {112.207836, -84.578666, -463.149397, 120.169159};
    double bbox3[4] = {129.886277, -82.332680, -445.470956, 120.390930};
    double bbox4[4] = {458.220043, -84.239039, -117.137190, 120.206585};
    double bbox5[4] = {456.713058, -80.354196, -118.644175, 120.539148};
    double bbox6[4] = {453.105328, -75.857422, -122.251904, 120.732760};
    double bbox7[4] = {-180, -90, 180, 90};

    assert(index.getClusters(bbox1, 1).size() == 26);
    assert(index.getClusters(bbox2, 1).size() == 27);
    assert(index.getClusters(bbox3, 1).size() == 26);
    assert(index.getClusters(bbox4, 1).size() == 25);
    assert(index.getClusters(bbox5, 1).size() == 25);
    assert(index.getClusters(bbox6, 1).size() == 25);
    assert(index.getClusters(bbox7, 1).size() == 61);

    // ----------------------- test 7 - getClusters -----------------------
    const auto getClustersFeatures = parseFeatures("test/fixtures/getClustersPlaces.json");

    mapbox::supercluster::Options getClustersOptions;
    mapbox::supercluster::Supercluster getClustersIndex(getClustersFeatures, getClustersOptions);

    double nonCrossingBbox[4] = {-179, -10, -177, 10};
    double crossingBbox[4] = {179, -10, -177, 10};

    const auto nonCrossingClusters = getClustersIndex.getClusters(nonCrossingBbox, 1);
    const auto crossingClusters = getClustersIndex.getClusters(crossingBbox, 1);

    assert(nonCrossingClusters.size() == crossingClusters.size());

    // ----------------------- test for generateId -----------------------
    mapbox::supercluster::Options generateIdoptions;
    generateIdoptions.generateId = true;
    mapbox::supercluster::Supercluster generateIdIndex(features, generateIdoptions);
    std::vector<uint64_t> ids;
    auto generateIdfeatures = generateIdIndex.getTile(0, 0, 0);
    for(const auto& feature : generateIdfeatures) {
        if (feature.properties.find("cluster") == feature.properties.end()) {
            ids.push_back(feature.id.get<uint64_t>());
        }
    }

    assert((ids == std::vector<uint64_t>{12, 20, 21, 22, 24, 28, 30, 62, 81, 118, 119, 125, 81, 118}));
}

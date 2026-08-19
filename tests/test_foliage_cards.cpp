#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "flora/geometry/branch_mesh.hpp"
#include "flora/geometry/foliage_cards.hpp"
#include "flora/geometry/material.hpp"
#include "flora/growth/simulation.hpp"
#include "flora/structure/foliage.hpp"

using namespace flora;

namespace {

struct CardedPlant {
    Plant plant;
    GrowthParameters parameters;
    std::vector<Vec3> tips;
    std::vector<FoliageCluster> clusters;
};

CardedPlant grownPlant(std::uint64_t seed, std::uint32_t steps) {
    CardedPlant result;
    result.parameters.shadeModel = ShadeModel::Off;
    result.parameters.maxPathLength = 6.0f;
    result.plant.seed = seed;
    plantSeedling(result.plant, result.parameters);
    GrowthContext context;
    grow(result.plant, result.parameters, ConstantEnvironment{}, steps, context);
    result.plant.graph.computeTipPositions(result.tips);
    computeFoliagePlacement(result.plant.graph, result.parameters, result.tips, result.clusters);
    return result;
}

std::vector<TwigCard> cardsFor(const CardedPlant& tree, const TwigCardParameters& parameters) {
    std::vector<TwigCard> cards;
    buildTwigCards(tree.plant.graph, tree.clusters, tree.tips, parameters, cards);
    return cards;
}

TwigCardParameters defaultCardParameters(const CardedPlant& tree) {
    TwigCardParameters parameters;
    parameters.maxBearingRadius = tree.parameters.leafMaxBearingRadius;
    parameters.seed = 17;
    return parameters;
}

} // namespace

TEST_CASE("twig cards: every foliage cluster is absorbed by exactly one card") {
    const CardedPlant tree = grownPlant(21, 12);
    REQUIRE(!tree.clusters.empty());
    const std::vector<TwigCard> cards = cardsFor(tree, defaultCardParameters(tree));
    REQUIRE(!cards.empty());

    std::size_t absorbed = 0;
    for (const TwigCard& card : cards) {
        absorbed += card.clusterCount;
        CHECK(card.clusterCount > 0); // leafless twigs get no card
    }
    CHECK(absorbed == tree.clusters.size());
}

TEST_CASE("twig cards: far fewer cards than clusters, and each is twig-sized") {
    const CardedPlant tree = grownPlant(21, 12);
    const TwigCardParameters parameters = defaultCardParameters(tree);
    const std::vector<TwigCard> cards = cardsFor(tree, parameters);

    CHECK(cards.size() < tree.clusters.size());
    for (const TwigCard& card : cards) {
        CHECK(card.height >= parameters.minHeight * parameters.sizeScale);
        CHECK(card.height <= parameters.maxLength * parameters.sizeScale);
        CHECK(card.width == doctest::Approx(card.height * parameters.aspect));
        CHECK(card.axis.length() == doctest::Approx(1.0f).epsilon(0.001));
    }
}

TEST_CASE("twig cards: a shorter card length splits runs into more cards") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters coarse = defaultCardParameters(tree);
    coarse.maxLength = 1.5f;
    TwigCardParameters fine = coarse;
    fine.maxLength = 0.3f;

    CHECK(cardsFor(tree, fine).size() > cardsFor(tree, coarse).size());
}

TEST_CASE("twig cards: placement is deterministic") {
    const CardedPlant tree = grownPlant(21, 12);
    const TwigCardParameters parameters = defaultCardParameters(tree);
    const std::vector<TwigCard> first = cardsFor(tree, parameters);
    const std::vector<TwigCard> second = cardsFor(tree, parameters);

    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].twigBase == second[i].twigBase);
        CHECK(first[i].roll == second[i].roll);
        CHECK(first[i].height == second[i].height);
        CHECK(first[i].base.x == second[i].base.x);
    }
}

TEST_CASE("twig cards: a different seed rolls the cards differently") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters first = defaultCardParameters(tree);
    TwigCardParameters second = first;
    second.seed = first.seed + 1;

    const std::vector<TwigCard> a = cardsFor(tree, first);
    const std::vector<TwigCard> b = cardsFor(tree, second);
    REQUIRE(a.size() == b.size());
    bool anyDifferent = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        anyDifferent = anyDifferent || a[i].roll != b[i].roll;
    }
    CHECK(anyDifferent);
}

TEST_CASE("twig card mesh: crossed quads, valid, deterministic") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters parameters = defaultCardParameters(tree);
    parameters.quadsPerCard = 2;
    const std::vector<TwigCard> cards = cardsFor(tree, parameters);

    Mesh mesh;
    generateTwigCardMesh(cards, parameters, mesh);
    std::string errors;
    CHECK(validateMesh(mesh, &errors));
    CHECK(errors.empty());
    CHECK(mesh.vertexCount() == cards.size() * parameters.quadsPerCard * 4);
    CHECK(mesh.triangleCount() == cards.size() * parameters.quadsPerCard * 2);
    for (const Vec2& uv : mesh.uvs) {
        CHECK(uv.x >= 0.0f);
        CHECK(uv.x <= 1.0f);
        CHECK(uv.y >= 0.0f);
        CHECK(uv.y <= 1.0f);
    }

    Mesh again;
    generateTwigCardMesh(cards, parameters, again);
    CHECK(again.positions == mesh.positions);
    CHECK(again.indices == mesh.indices);
}

TEST_CASE("twig card mesh: cards hang off their attachment point") {
    const CardedPlant tree = grownPlant(21, 12);
    const TwigCardParameters parameters = defaultCardParameters(tree);
    const std::vector<TwigCard> cards = cardsFor(tree, parameters);
    REQUIRE(!cards.empty());

    Mesh mesh;
    generateTwigCardMesh(cards, parameters, mesh);
    Vec3 minimum;
    Vec3 maximum;
    REQUIRE(computeBounds(mesh, minimum, maximum));

    // A card reaches at most one card diagonal beyond the twig it replaces.
    Vec3 twigMinimum = cards.front().base;
    Vec3 twigMaximum = cards.front().base;
    float longest = 0.0f;
    for (const TwigCard& card : cards) {
        twigMinimum.x = std::min(twigMinimum.x, card.base.x);
        twigMinimum.y = std::min(twigMinimum.y, card.base.y);
        twigMinimum.z = std::min(twigMinimum.z, card.base.z);
        twigMaximum.x = std::max(twigMaximum.x, card.base.x);
        twigMaximum.y = std::max(twigMaximum.y, card.base.y);
        twigMaximum.z = std::max(twigMaximum.z, card.base.z);
        longest = std::max(longest, card.height + card.width);
    }
    CHECK(minimum.z >= twigMinimum.z - longest);
    CHECK(maximum.z <= twigMaximum.z + longest);
}

TEST_CASE("branch mesh: culling thin stems removes the geometry the cards replace") {
    const CardedPlant tree = grownPlant(21, 12);

    BranchMeshParameters full;
    Mesh fullMesh;
    generateBranchMesh(tree.plant.graph, full, fullMesh);

    BranchMeshParameters culled = full;
    culled.cullBelowRadius = tree.parameters.leafMaxBearingRadius;
    Mesh culledMesh;
    generateBranchMesh(tree.plant.graph, culled, culledMesh);

    CHECK(culledMesh.triangleCount() < fullMesh.triangleCount());
    std::string errors;
    CHECK(validateMesh(culledMesh, &errors));

    // No swept ring may sit on a stem thinner than the cut.
    float thinnest = 1.0e30f;
    for (Index i = 0; i < static_cast<Index>(tree.plant.graph.internodeCount()); ++i) {
        const Internode& node = tree.plant.graph.internode(i);
        if (node.alive && node.radius >= culled.cullBelowRadius) {
            thinnest = std::min(thinnest, node.radius);
        }
    }
    CHECK(thinnest >= culled.cullBelowRadius);

    BranchMeshParameters everything = full;
    everything.cullBelowRadius = 1.0e6f;
    Mesh emptyMesh;
    generateBranchMesh(tree.plant.graph, everything, emptyMesh);
    CHECK(emptyMesh.triangleCount() == 0);
}

TEST_CASE("materials: valid file parses and reaches the MTL library") {
    const std::string text = R"({
        "format": "flora-materials",
        "version": 1,
        "name": "Oak",
        "bark": {"color": "bark.png", "normal": "bark_n.png", "uvLengthScale": 1.5},
        "foliage": {"color": "twig.png", "aspect": 0.8, "pivotU": 0.4, "alphaCutoff": 0.6}
    })";

    MaterialSet materials;
    std::string error;
    REQUIRE(loadMaterialSetFromJson(text, materials, &error));
    CHECK(error.empty());
    CHECK(materials.name == "Oak");
    CHECK(materials.bark.color == "bark.png");
    CHECK(materials.bark.normal == "bark_n.png");
    CHECK(materials.barkUvLengthScale == doctest::Approx(1.5f));
    CHECK(materials.foliageAspect == doctest::Approx(0.8f));
    CHECK(materials.foliagePivotU == doctest::Approx(0.4f));
    CHECK(materials.foliageAlphaCutoff == doctest::Approx(0.6f));

    const std::string library = materialLibraryText(materials, "assets/");
    CHECK(library.find("newmtl bark") != std::string::npos);
    CHECK(library.find("newmtl foliage") != std::string::npos);
    CHECK(library.find("map_Kd assets/bark.png") != std::string::npos);
    CHECK(library.find("norm assets/bark_n.png") != std::string::npos);
    // Foliage cards are alpha-cut from their colour map.
    CHECK(library.find("map_d assets/twig.png") != std::string::npos);
}

TEST_CASE("materials: strict parsing rejects bad files") {
    MaterialSet materials;
    std::string error;

    CHECK_FALSE(loadMaterialSetFromJson("{", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(R"({"format": "flora-materials"})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-species", "version": 1})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-materials", "version": 99})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-materials", "version": 1, "leaves": {}})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-materials", "version": 1, "bark": {"colour": "x.png"}})", materials,
        &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-materials", "version": 1, "foliage": {"aspect": 0}})", materials,
        &error));
}

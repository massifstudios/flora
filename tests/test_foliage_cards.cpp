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
    parameters.clusterSpacing = tree.parameters.leafClusterSpacing;
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

TEST_CASE("twig card variants: conditions decide, and every card lands in a section") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters parameters = defaultCardParameters(tree);

    // Split the population on its own median so both variants are exercised
    // whatever the fixture grows.
    std::vector<float> densities;
    for (const TwigCard& card : cardsFor(tree, parameters)) {
        densities.push_back(card.foliageDensity);
    }
    REQUIRE(densities.size() > 4);
    std::sort(densities.begin(), densities.end());
    const float split = densities[densities.size() / 2];

    TwigCardVariant sparse;
    sparse.maxFoliageDensity = split;
    TwigCardVariant dense;
    dense.minFoliageDensity = split;
    parameters.variants = {sparse, dense};

    const std::vector<TwigCard> cards = cardsFor(tree, parameters);
    std::vector<std::size_t> perVariant(2, 0);
    for (const TwigCard& card : cards) {
        REQUIRE(card.variant < 2);
        perVariant[card.variant] += 1;
        // The chosen variant must be one that accepts the card, never a
        // fallback: these two bands cover the whole range.
        CHECK(parameters.variants[card.variant].accepts(card));
    }
    CHECK(perVariant[0] > 0);
    CHECK(perVariant[1] > 0);

    Mesh mesh;
    std::vector<MeshSection> sections;
    generateTwigCardMesh(cards, parameters, mesh, &sections);
    CHECK(validateMesh(mesh));
    REQUIRE(sections.size() == 2);
    CHECK(sections[0].firstIndex == 0);
    CHECK(sections[1].firstIndex == sections[0].indexCount);
    // Sections partition the mesh: no triangle is dropped or drawn twice.
    CHECK(sections[0].indexCount + sections[1].indexCount == mesh.indices.size());
    for (std::size_t i = 0; i < sections.size(); ++i) {
        CHECK(sections[i].material == i);
        CHECK(sections[i].indexCount == perVariant[i] * parameters.quadsPerCard * 6);
    }
}

TEST_CASE("twig card variants: branch fraction separates inner twigs from outer") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters parameters = defaultCardParameters(tree);

    TwigCardVariant everywhere;
    TwigCardVariant innerOnly;
    innerOnly.maxBranchFraction = 0.3f;
    parameters.variants = {everywhere, innerOnly};

    bool sawInner = false;
    for (const TwigCard& card : cardsFor(tree, parameters)) {
        CHECK(card.branchFraction >= 0.0f);
        CHECK(card.branchFraction <= 1.0f);
        if (card.variant == 1) {
            // A shed-twig texture must never reach the outer bough, whatever
            // the weighted draw does.
            CHECK(card.branchFraction <= 0.3f);
            sawInner = true;
        }
    }
    CHECK(sawInner);
}

TEST_CASE("twig card variants: an unmatched card falls back to the first") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters parameters = defaultCardParameters(tree);

    TwigCardVariant unreachable; // accepts nothing
    unreachable.minClusters = 100000;
    parameters.variants = {unreachable, unreachable};

    const std::vector<TwigCard> cards = cardsFor(tree, parameters);
    REQUIRE(!cards.empty());
    for (const TwigCard& card : cards) {
        CHECK(card.variant == 0);
    }
}

TEST_CASE("twig card variants: selection is deterministic and roll is unaffected") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters plain = defaultCardParameters(tree);
    const std::vector<TwigCard> withoutVariants = cardsFor(tree, plain);

    TwigCardVariant low;
    low.maxFoliageDensity = 1.0f;
    TwigCardVariant high;
    high.minFoliageDensity = 1.0f;
    TwigCardParameters banded = plain;
    banded.variants = {low, high};

    const std::vector<TwigCard> first = cardsFor(tree, banded);
    const std::vector<TwigCard> again = cardsFor(tree, banded);
    REQUIRE(first.size() == withoutVariants.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].variant == again[i].variant);
        // The variant draw is appended to the card's stream, so adding an
        // atlas must not rotate the cards that were already placed.
        CHECK(first[i].roll == doctest::Approx(withoutVariants[i].roll));
    }
}

TEST_CASE("materials: valid file parses and reaches the MTL library") {
    const std::string text = R"({
        "format": "flora-materials",
        "version": 3,
        "name": "Oak",
        "bark": {"color": "bark.png", "normal": "bark_n.png", "uvLengthScale": 1.5},
        "foliage": {"aspect": 0.8, "pivotU": 0.4, "alphaCutoff": 0.6,
                    "variants": [{"color": "twig.png"}]}
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
    REQUIRE(materials.foliage.size() == 1);
    CHECK(materials.foliage[0].maps.color == "twig.png");

    const std::string library = materialLibraryText(materials, "assets/");
    CHECK(library.find("newmtl bark") != std::string::npos);
    // A lone variant keeps the unnumbered group name.
    CHECK(library.find("newmtl foliage\n") != std::string::npos);
    CHECK(library.find("map_Kd assets/bark.png") != std::string::npos);
    CHECK(library.find("norm assets/bark_n.png") != std::string::npos);
    // Foliage cards are alpha-cut from their colour map.
    CHECK(library.find("map_d assets/twig.png") != std::string::npos);
}

TEST_CASE("materials: a variant list becomes numbered materials with depth maps") {
    const std::string text = R"({
        "format": "flora-materials",
        "version": 3,
        "name": "Spruce",
        "bark": {"color": "bark.png"},
        "foliage": {"variants": [
            {"color": "dense.png", "depth": "dense_d.png", "minDensity": 0.9},
            {"color": "bare.png", "maxDensity": 0.4, "weight": 2.0}
        ]}
    })";

    MaterialSet materials;
    std::string error;
    REQUIRE(loadMaterialSetFromJson(text, materials, &error));
    REQUIRE(materials.foliage.size() == 2);
    CHECK(materials.foliage[0].selector.minFoliageDensity == doctest::Approx(0.9f));
    CHECK(materials.foliage[1].selector.maxFoliageDensity == doctest::Approx(0.4f));
    CHECK(materials.foliage[1].selector.weight == doctest::Approx(2.0f));
    CHECK(foliageMaterialName(materials, 0) == "foliage_0");
    CHECK(foliageMaterialName(materials, 1) == "foliage_1");
    CHECK(foliageSelectors(materials).size() == 2);

    const std::string library = materialLibraryText(materials, "");
    CHECK(library.find("newmtl foliage_0") != std::string::npos);
    CHECK(library.find("newmtl foliage_1") != std::string::npos);
    CHECK(library.find("disp dense_d.png") != std::string::npos);
}

TEST_CASE("materials: strict parsing rejects bad files") {
    MaterialSet materials;
    std::string error;
    const std::string prefix = R"({"format": "flora-materials", "version": 3, )";

    CHECK_FALSE(loadMaterialSetFromJson("{", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(R"({"format": "flora-materials"})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-species", "version": 3})", materials, &error));
    // Development reset: only the current version loads, in either direction.
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-materials", "version": 99})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        R"({"format": "flora-materials", "version": 1,
            "foliage": {"variants": [{"color": "x.png"}]}})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(prefix + R"("leaves": {}})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(prefix + R"("bark": {"colour": "x.png"}})", materials,
                                        &error));
    // Foliage must carry at least one variant, and every variant a colour map.
    CHECK_FALSE(loadMaterialSetFromJson(prefix + R"("foliage": {"aspect": 0.8}})", materials,
                                        &error));
    CHECK_FALSE(loadMaterialSetFromJson(prefix + R"("foliage": {"variants": []}})", materials,
                                        &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        prefix + R"("foliage": {"variants": [{"depth": "x.png"}]}})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        prefix + R"("foliage": {"variants": [{"color": "x.png", "shade": 1}]}})", materials,
        &error));
    // An empty condition range can never match, so it is an authoring error.
    CHECK_FALSE(loadMaterialSetFromJson(
        prefix + R"("foliage": {"variants": [
            {"color": "x.png", "minDensity": 0.9, "maxDensity": 0.2}]}})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        prefix + R"("foliage": {"aspect": 0, "variants": [{"color": "x.png"}]}})", materials,
        &error));
    // A card must emit at least itself, and the size jitter cannot reach zero.
    CHECK_FALSE(loadMaterialSetFromJson(
        prefix + R"("foliage": {"instances": 0, "variants": [{"color": "x.png"}]}})", materials,
        &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        prefix + R"("foliage": {"instanceScaleJitter": 1.0,
                                "variants": [{"color": "x.png"}]}})", materials, &error));
    CHECK_FALSE(loadMaterialSetFromJson(
        prefix + R"("foliage": {"instanceSpread": -0.1,
                                "variants": [{"color": "x.png"}]}})", materials, &error));
}

TEST_CASE("materials: card instancing round-trips through the foliage block") {
    const std::string text = R"({
        "format": "flora-materials",
        "version": 3,
        "name": "Fan",
        "foliage": {
            "instances": 4, "instanceSpread": 0.7, "instanceScaleJitter": 0.35,
            "variants": [{"color": "twig.png"}]
        }
    })";
    MaterialSet materials;
    std::string error;
    REQUIRE_MESSAGE(loadMaterialSetFromJson(text, materials, &error), error);
    CHECK(materials.foliageInstances == 4);
    CHECK(materials.foliageInstanceSpread == doctest::Approx(0.7f));
    CHECK(materials.foliageInstanceScaleJitter == doctest::Approx(0.35f));
}

TEST_CASE("card instancing: siblings multiply the mesh and leave the first card alone") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters single = defaultCardParameters(tree);
    single.quadsPerCard = 2;
    const std::vector<TwigCard> cards = cardsFor(tree, single);
    REQUIRE(!cards.empty());

    TwigCardParameters fanned = single;
    fanned.instancesPerCard = 3;

    Mesh one;
    Mesh three;
    generateTwigCardMesh(cards, single, one);
    generateTwigCardMesh(cards, fanned, three);

    CHECK(three.vertexCount() == one.vertexCount() * 3);
    CHECK(three.triangleCount() == one.triangleCount() * 3);
    std::string errors;
    CHECK_MESSAGE(validateMesh(three, &errors), errors);

    // Instance 0 is the card itself: the first card's quads must be untouched,
    // so raising the count only ever adds geometry around what was already there.
    for (std::size_t i = 0; i < single.quadsPerCard * 4; ++i) {
        CHECK(three.positions[i].x == doctest::Approx(one.positions[i].x));
        CHECK(three.positions[i].y == doctest::Approx(one.positions[i].y));
        CHECK(three.positions[i].z == doctest::Approx(one.positions[i].z));
    }

    Mesh again;
    generateTwigCardMesh(cards, fanned, again);
    CHECK(again.positions == three.positions);
    CHECK(again.indices == three.indices);
}

TEST_CASE("card instancing: siblings stay inside the twig they stand for") {
    const CardedPlant tree = grownPlant(21, 12);
    TwigCardParameters fanned = defaultCardParameters(tree);
    fanned.instancesPerCard = 4;
    fanned.instanceSpread = 0.5f;
    fanned.instanceScaleJitter = 0.25f;
    const std::vector<TwigCard> cards = cardsFor(tree, fanned);
    REQUIRE(!cards.empty());

    Mesh mesh;
    std::vector<MeshSection> sections;
    generateTwigCardMesh(cards, fanned, mesh, &sections);

    // Sections must still partition the mesh once instancing multiplies it.
    std::size_t sectioned = 0;
    for (const MeshSection& section : sections) {
        sectioned += section.indexCount;
    }
    CHECK(sectioned == mesh.indices.size());

    // A fanned card must not fling geometry across the crown: every vertex
    // stays within a card's own reach of some card attachment point.
    float worst = 0.0f;
    for (const Vec3& position : mesh.positions) {
        float nearest = 1.0e30f;
        for (const TwigCard& card : cards) {
            nearest = std::min(nearest, (position - card.base).length() /
                                            std::max(card.height, 1.0e-4f));
        }
        worst = std::max(worst, nearest);
    }
    MESSAGE("farthest vertex sits " << worst << " card heights from an attachment");
    CHECK(worst < 4.0f);
}

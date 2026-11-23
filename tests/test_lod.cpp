#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "flora/geometry/lod.hpp"
#include "flora/growth/simulation.hpp"
#include "flora/structure/foliage.hpp"

using namespace flora;

namespace {

struct Subject {
    Plant plant;
    GrowthParameters parameters;
    std::vector<Vec3> tips;
    std::vector<FoliageCluster> clusters;
};

Subject grownSubject(std::uint64_t seed, std::uint32_t steps) {
    Subject subject;
    subject.parameters.shadeModel = ShadeModel::Off;
    subject.parameters.maxPathLength = 8.0f;
    subject.plant.seed = seed;
    plantSeedling(subject.plant, subject.parameters);
    GrowthContext context;
    grow(subject.plant, subject.parameters, ConstantEnvironment{}, steps, context);
    subject.plant.graph.computeTipPositions(subject.tips);
    computeFoliagePlacement(subject.plant.graph, subject.parameters, subject.tips,
                            subject.clusters);
    return subject;
}

TwigCardParameters baseCards(const Subject& subject) {
    TwigCardParameters cards;
    cards.maxBearingRadius = subject.parameters.leafMaxBearingRadius;
    cards.clusterSpacing = subject.parameters.leafClusterSpacing;
    cards.seed = 5;
    return cards;
}

LodMesh buildLevel(const Subject& subject, std::uint32_t level) {
    LodMesh mesh;
    buildLodMesh(subject.plant.graph, subject.clusters, subject.tips, level,
                 BranchMeshParameters{}, baseCards(subject), mesh);
    return mesh;
}

} // namespace

TEST_CASE("lod levels get cheaper the further out they are") {
    const Subject subject = grownSubject(31, 14);
    REQUIRE(!subject.clusters.empty());

    std::vector<std::size_t> vertices;
    for (std::uint32_t level = 0; level < kLodLevels; ++level) {
        const LodMesh mesh = buildLevel(subject, level);
        vertices.push_back(mesh.vertexCount());
        std::string errors;
        CHECK_MESSAGE(validateMesh(mesh.bark, &errors), errors);
        CHECK_MESSAGE(validateMesh(mesh.foliage, &errors), errors);
    }
    for (std::size_t level = 1; level < vertices.size(); ++level) {
        CHECK(vertices[level] < vertices[level - 1]);
    }
    MESSAGE("lod vertices " << vertices[0] << " / " << vertices[1] << " / " << vertices[2] << " / "
                            << vertices[3]);
}

TEST_CASE("the impostor is a handful of quads and stands on the ground") {
    const Subject subject = grownSubject(31, 14);
    const LodMesh impostor = buildLevel(subject, kLodLevels - 1);

    CHECK(impostor.bark.vertexCount() == 0); // no wood survives to the impostor
    CHECK(impostor.foliage.vertexCount() == kImpostorVertices);
    CHECK(impostor.foliage.triangleCount() == 2);

    Vec3 minimum;
    Vec3 maximum;
    REQUIRE(computeBounds(impostor.foliage, minimum, maximum));
    CHECK(minimum.z == doctest::Approx(0.0f)); // planted, not hovering

    // It has to cover the crown it replaces, or the tree shrinks at the swap.
    const LodMesh near = buildLevel(subject, 0);
    Vec3 nearMinimum;
    Vec3 nearMaximum;
    REQUIRE(computeBounds(near.foliage, nearMinimum, nearMaximum));
    CHECK(maximum.z >= nearMaximum.z * 0.85f);
}

TEST_CASE("the impostor quad spans the crown the near level drew") {
    const Subject subject = grownSubject(31, 14);
    const LodMesh impostor = buildLevel(subject, kLodLevels - 1);
    REQUIRE(impostor.foliage.vertexCount() == kImpostorVertices);

    // The outline is in the atlas alpha now, not in the geometry, so the quad
    // has one job: cover exactly the crown, from the ground up. Too small clips
    // the boughs the bake drew; too large floats the tree inside a margin.
    const LodMesh near = buildLevel(subject, 0);
    Vec3 nearMinimum;
    Vec3 nearMaximum;
    REQUIRE(computeBounds(near.foliage, nearMinimum, nearMaximum));
    Vec3 minimum;
    Vec3 maximum;
    REQUIRE(computeBounds(impostor.foliage, minimum, maximum));

    CHECK(minimum.z == doctest::Approx(0.0f)); // planted, not hovering
    CHECK(maximum.z == doctest::Approx(nearMaximum.z).epsilon(0.05));

    // Covers the crown across the bearing it faces -- a quad narrower than the
    // boughs clips what the bake drew -- without floating it in a wide margin.
    const float nearWidth = std::max(nearMaximum.x - nearMinimum.x, nearMaximum.y - nearMinimum.y);
    const float width = maximum.x - minimum.x;
    CHECK(minimum.x <= nearMinimum.x);
    CHECK(maximum.x >= nearMaximum.x);
    CHECK(width < nearWidth * 1.5f);

    // UVs are one whole tile: the atlas cell is the renderer's to choose.
    for (const Vec2& uv : impostor.foliage.uvs) {
        CHECK((uv.x == doctest::Approx(0.0f) || uv.x == doctest::Approx(1.0f)));
        CHECK((uv.y == doctest::Approx(0.0f) || uv.y == doctest::Approx(1.0f)));
    }
}

TEST_CASE("the impostor atlas lookup is the inverse of the bake") {
    // The bake writes tile i at impostorAngleYaw(i); the draw has to find the
    // same tile from the same yaw, or the tree faces the wrong way.
    for (std::uint32_t index = 0; index < kImpostorAngles; ++index) {
        const ImpostorTile tile = impostorTileForYaw(impostorAngleYaw(index));
        CHECK(tile.index == index);
    }

    // And a yaw between two bakes rounds to the nearer, in both directions.
    const float step = 2.0f * 3.1415927f / static_cast<float>(kImpostorAngles);
    for (std::uint32_t index = 0; index < kImpostorAngles; ++index) {
        const float yaw = impostorAngleYaw(index);
        CHECK(impostorTileForYaw(yaw + step * 0.4f).index == index);
        CHECK(impostorTileForYaw(yaw - step * 0.4f).index == index);
        CHECK(impostorTileForYaw(yaw + step * 2.0f).index == (index + 2) % kImpostorAngles);
    }

    // Tiles are distinct cells of the 4x2 grid, and every cell is used.
    std::vector<float> corners;
    for (std::uint32_t index = 0; index < kImpostorAngles; ++index) {
        const ImpostorTile tile = impostorTileForYaw(impostorAngleYaw(index));
        CHECK(tile.uOffset + tile.uScale <= 1.0f + 1.0e-5f);
        CHECK(tile.vOffset + tile.vScale <= 1.0f + 1.0e-5f);
        corners.push_back(tile.uOffset * 100.0f + tile.vOffset);
    }
    std::sort(corners.begin(), corners.end());
    CHECK(std::unique(corners.begin(), corners.end()) == corners.end());
}

TEST_CASE("a coarse level draws the near level's cards, not its own") {
    // The whole point of thinning rather than re-deriving: a card a coarse level
    // draws stands on a twig the near level also carded, in the place the near
    // level put it. Anything else moves the silhouette on the transition.
    const Subject subject = grownSubject(31, 14);
    const TwigCardParameters authored = baseCards(subject);

    std::vector<TwigCard> nearCards;
    REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, 0, authored,
                          nearCards));
    REQUIRE(!nearCards.empty());
    std::vector<Index> nearBases;
    std::vector<Vec3> nearAt;
    for (const TwigCard& card : nearCards) {
        nearBases.push_back(card.twigBase);
        nearAt.push_back(card.base);
    }

    std::size_t previous = nearCards.size();
    for (std::uint32_t level = 1; level + 1 < kLodLevels; ++level) {
        std::vector<TwigCard> cards;
        REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, level, authored,
                              cards));
        CHECK(cards.size() <= previous);
        previous = cards.size();
        for (const TwigCard& card : cards) {
            const auto found = std::find(nearBases.begin(), nearBases.end(), card.twigBase);
            REQUIRE(found != nearBases.end());
            const Vec3 moved = card.base - nearAt[static_cast<std::size_t>(found - nearBases.begin())];
            CHECK(moved.length() == doctest::Approx(0.0f)); // same twig, same place
        }
    }
}

TEST_CASE("thinning culls the interior and keeps the surfaces") {
    const Subject subject = grownSubject(31, 14);
    std::vector<TwigCard> nearCards;
    REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, 0,
                          baseCards(subject), nearCards));
    REQUIRE(nearCards.size() > 20);
    const CrownEnvelope envelope = measureCrownEnvelope(nearCards);
    REQUIRE(envelope.valid());

    auto surfaceShare = [](const std::vector<TwigCard>& cards) {
        std::size_t surface = 0;
        for (const TwigCard& card : cards) {
            if (card.crownDepth <= 0.12f || card.branchFraction >= 0.6f ||
                card.crownDepth >= 0.75f) {
                ++surface;
            }
        }
        return static_cast<float>(surface) / static_cast<float>(cards.size());
    };

    std::vector<TwigCard> thinned;
    thinTwigCards(nearCards, 0.52f, envelope, thinned);
    CHECK(thinned.size() < nearCards.size());

    // What survives is more surface than what went in: the occluded core is
    // where most of the budget comes from, because it is the part no bearing
    // can see.
    CHECK(surfaceShare(thinned) > surfaceShare(nearCards));

    // But the core is sampled, not deleted. Emptying it entirely is what left
    // the bole bare with the outer tips floating off it.
    std::size_t core = 0;
    std::size_t nearCore = 0;
    for (const TwigCard& card : thinned) {
        if (card.crownDepth > 0.12f && card.branchFraction < 0.6f && card.crownDepth < 0.75f) {
            ++core;
        }
    }
    for (const TwigCard& card : nearCards) {
        if (card.crownDepth > 0.12f && card.branchFraction < 0.6f && card.crownDepth < 0.75f) {
            ++nearCore;
        }
    }
    CHECK(core > nearCore / 4);

    // The leader still carries every card it had. A stride that treats the apex
    // like the rest of the crown deletes it, which reads as the spire fading.
    std::size_t apex = 0;
    std::size_t nearApex = 0;
    float nearTop = -1.0e30f;
    float thinnedTop = -1.0e30f;
    for (const TwigCard& card : nearCards) {
        nearTop = std::max(nearTop, card.base.z + card.height);
        if (card.crownDepth <= 0.12f) {
            ++nearApex;
        }
    }
    for (const TwigCard& card : thinned) {
        thinnedTop = std::max(thinnedTop, card.base.z + card.height);
        if (card.crownDepth <= 0.12f) {
            ++apex;
        }
    }
    CHECK(apex == nearApex);
    CHECK(thinnedTop >= nearTop * 0.98f);
}

TEST_CASE("a grown survivor never reaches past the near crown's envelope") {
    const Subject subject = grownSubject(31, 14);
    std::vector<TwigCard> nearCards;
    REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, 0,
                          baseCards(subject), nearCards));
    const CrownEnvelope envelope = measureCrownEnvelope(nearCards);
    REQUIRE(envelope.valid());

    std::vector<TwigCard> thinned;
    thinTwigCards(nearCards, 0.52f, envelope, thinned);

    // Two bounds, and the reason for each: the growth cap stops a plate bridging
    // the gap between two whorls and welding the shelves into a wall, and the
    // envelope stops the crown swelling on the transition.
    for (std::size_t i = 0, near = 0; i < thinned.size(); ++i) {
        while (near < nearCards.size() && nearCards[near].twigBase != thinned[i].twigBase) {
            ++near;
        }
        REQUIRE(near < nearCards.size());
        CHECK(thinned[i].height <= nearCards[near].height * 1.30f + 1.0e-4f);
        CHECK(thinned[i].height >= nearCards[near].height - 1.0e-4f);

        const Vec3 axis = thinned[i].axis.normalizedOr(kUpAxis);
        const Vec3 tip = thinned[i].base + axis * thinned[i].height;
        CHECK(tip.z <= envelope.top + 1.0e-3f);
    }
}

TEST_CASE("thinning hits the retention it was asked for, class by class") {
    const Subject subject = grownSubject(31, 14);
    std::vector<TwigCard> nearCards;
    REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, 0,
                          baseCards(subject), nearCards));
    const CrownEnvelope envelope = measureCrownEnvelope(nearCards);

    // The bias tilts which cards go, never how many: a level that asks for half
    // its cards and gets a third is the level that read as a bare pole.
    for (const float keep : {0.35f, 0.52f, 0.75f}) {
        std::vector<TwigCard> thinned;
        thinTwigCards(nearCards, keep, envelope, thinned);
        const float got =
            static_cast<float>(thinned.size()) / static_cast<float>(nearCards.size());
        CHECK(got == doctest::Approx(keep).epsilon(0.08));
    }

    // And the level the ladder actually draws lands inside its own budget.
    std::vector<TwigCard> level2;
    REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, 2,
                          baseCards(subject), level2));
    const float retained =
        static_cast<float>(level2.size()) / static_cast<float>(nearCards.size());
    CHECK(retained >= 0.50f);
    CHECK(retained <= 0.60f);
}

TEST_CASE("thinning is a stride, so it is deterministic and spatially even") {
    const Subject subject = grownSubject(31, 14);
    std::vector<TwigCard> nearCards;
    REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, 0,
                          baseCards(subject), nearCards));
    const CrownEnvelope envelope = measureCrownEnvelope(nearCards);

    std::vector<TwigCard> first;
    std::vector<TwigCard> second;
    thinTwigCards(nearCards, 0.52f, envelope, first);
    thinTwigCards(nearCards, 0.52f, envelope, second);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].twigBase == second[i].twigBase);
    }

    // Keeping everything is the identity, not a rebuild.
    std::vector<TwigCard> all;
    thinTwigCards(nearCards, 1.0f, envelope, all);
    CHECK(all.size() == nearCards.size());
}

TEST_CASE("a coarse level tiles its twig texture instead of magnifying it") {
    const Subject subject = grownSubject(31, 14);

    // The picture shows one near-level twig. A plate that stands for a longer
    // run of stem has to show more twigs, or needles grow with viewing distance.
    float unit = 0.0f;
    std::vector<float> largest;
    for (std::uint32_t level = 0; level + 1 < kLodLevels; ++level) {
        BranchMeshParameters branches;
        TwigCardParameters cards = baseCards(subject);
        REQUIRE(applyLodProfile(level, branches, cards));
        if (level == 0) {
            unit = cards.textureTwigLength;
            CHECK(unit > 0.0f);
        }
        CHECK(cards.textureTwigLength == doctest::Approx(unit)); // one unit for every level

        const LodMesh mesh = buildLevel(subject, level);
        float span = 0.0f;
        for (const Vec2& uv : mesh.foliage.uvs) {
            span = std::max(span, std::max(uv.x, uv.y));
        }
        largest.push_back(span);
    }
    CHECK(largest[0] == doctest::Approx(1.0f)); // near: one card really is one twig
    CHECK(largest[kLodLevels - 2] > 1.0f);      // far: a grown survivor repeats it

    // No plate anywhere stretches the picture past the size it was shot at.
    for (std::uint32_t level = 0; level + 1 < kLodLevels; ++level) {
        std::vector<TwigCard> built;
        REQUIRE(buildLodCards(subject.plant.graph, subject.clusters, subject.tips, level,
                              baseCards(subject), built));
        for (const TwigCard& card : built) {
            const float tiles = std::ceil(card.height / unit - 1.0e-4f);
            CHECK(card.height <= std::max(1.0f, tiles) * unit * 1.001f);
        }
    }
}

TEST_CASE("a level never double-represents its own twigs") {
    // The card threshold and the mesh cull are one boundary: wood a card's
    // photograph already contains must not also be swept into the bark mesh.
    for (std::uint32_t level = 0; level < kLodLevels; ++level) {
        BranchMeshParameters branches;
        TwigCardParameters cards;
        cards.maxBearingRadius = 0.02f;
        REQUIRE(applyLodProfile(level, branches, cards));
        CHECK(branches.cullBelowRadius == doctest::Approx(cards.maxBearingRadius));
    }
}

TEST_CASE("a level only ever reduces the configuration it was given") {
    BranchMeshParameters baseBranches;
    TwigCardParameters baseCardParameters;
    baseCardParameters.maxBearingRadius = 0.02f;

    BranchMeshParameters previousBranches = baseBranches;
    TwigCardParameters previousCards = baseCardParameters;
    REQUIRE(applyLodProfile(0, previousBranches, previousCards));
    for (std::uint32_t level = 1; level < kLodLevels; ++level) {
        BranchMeshParameters branches = baseBranches;
        TwigCardParameters cards = baseCardParameters;
        REQUIRE(applyLodProfile(level, branches, cards));
        CHECK(branches.radialSegments <= previousBranches.radialSegments);
        CHECK(branches.branchRadialSegments <= previousBranches.branchRadialSegments);
        CHECK(cards.instancesPerCard <= previousCards.instancesPerCard);
        CHECK(lodCardKeepFraction(level) <= lodCardKeepFraction(level - 1));
        // Card sizing is common to the ladder. A level that redefined what a
        // twig is, or how large a plate sits, would be deriving its own cards
        // instead of thinning the near ones.
        CHECK(cards.maxBearingRadius == doctest::Approx(previousCards.maxBearingRadius));
        CHECK(cards.maxLength == doctest::Approx(previousCards.maxLength));
        CHECK(cards.sizeScale == doctest::Approx(previousCards.sizeScale));
        CHECK(cards.minHeight == doctest::Approx(previousCards.minHeight));
        previousBranches = branches;
        previousCards = cards;
    }
}

TEST_CASE("an out-of-range level is rejected and changes nothing") {
    BranchMeshParameters branches;
    TwigCardParameters cards;
    const BranchMeshParameters branchesBefore = branches;
    const std::uint32_t segmentsBefore = branches.radialSegments;
    const std::uint32_t instancesBefore = cards.instancesPerCard;

    CHECK_FALSE(applyLodProfile(kLodLevels, branches, cards));
    CHECK(branches.radialSegments == segmentsBefore);
    CHECK(branches.cullBelowRadius == doctest::Approx(branchesBefore.cullBelowRadius));
    CHECK(cards.instancesPerCard == instancesBefore);

    // A rejected level also produces nothing rather than a partial mesh.
    const Subject subject = grownSubject(31, 10);
    LodMesh mesh;
    buildLodMesh(subject.plant.graph, subject.clusters, subject.tips, kLodLevels,
                 BranchMeshParameters{}, baseCards(subject), mesh);
    CHECK(mesh.vertexCount() == 0);
    CHECK(mesh.cards == 0);
}

TEST_CASE("lod meshes are deterministic and sections partition the foliage") {
    const Subject subject = grownSubject(31, 14);
    for (std::uint32_t level = 0; level < kLodLevels; ++level) {
        const LodMesh first = buildLevel(subject, level);
        const LodMesh second = buildLevel(subject, level);
        CHECK(first.foliage.positions == second.foliage.positions);
        CHECK(first.bark.positions == second.bark.positions);
        CHECK(first.foliage.indices == second.foliage.indices);

        std::size_t sectioned = 0;
        for (const MeshSection& section : first.sections) {
            sectioned += section.indexCount;
        }
        CHECK(sectioned == first.foliage.indices.size());
    }
}

TEST_CASE("an empty crown produces an empty impostor rather than a degenerate quad") {
    Mesh mesh;
    std::vector<MeshSection> sections;
    generateImpostorMesh(measureCrownEnvelope({}), mesh, &sections);
    CHECK(mesh.vertexCount() == 0);
    CHECK(sections.empty());
}

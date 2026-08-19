#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "flora/environment/shadow_grid.hpp"
#include "flora/growth/simulation.hpp"

using namespace flora;

// Whole-tree form: the mechanisms that decide whether a plant reads as a tree
// with a trunk or as a coppiced clump of equals.

namespace {

Plant grownPlant(std::uint64_t seed, std::uint32_t steps, const GrowthParameters& parameters) {
    Plant plant;
    plant.seed = seed;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{}, steps, context);
    return plant;
}

GrowthParameters treeParameters() {
    GrowthParameters parameters;
    parameters.maxPathLength = 12.0f;
    parameters.gnarl = 0.0f;
    return parameters;
}

// Mean inclination of live internodes of a given order, in degrees above
// horizontal (90 = straight up).
float meanInclination(const StemGraph& graph, std::uint16_t order) {
    float sum = 0.0f;
    std::size_t counted = 0;
    for (const Internode& node : graph.internodes()) {
        if (!node.alive || node.branchOrder != order) {
            continue;
        }
        sum += std::asin(std::clamp(node.direction.normalizedOr(kUpAxis).z, -1.0f, 1.0f));
        ++counted;
    }
    return counted > 0 ? sum / static_cast<float>(counted) * 57.29578f : 0.0f;
}

std::size_t liveOrderZeroCount(const StemGraph& graph) {
    std::size_t count = 0;
    for (const Internode& node : graph.internodes()) {
        if (node.alive && node.branchOrder == 0) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("branches are steered as their own organ, not as trunks") {
    GrowthParameters shared = treeParameters();
    shared.gravitropism = 0.25f;
    shared.lateralGravitropism = 0.25f; // branches behave like leaders

    GrowthParameters plagiotropic = shared;
    plagiotropic.lateralGravitropism = 0.0f;

    const Plant upright = grownPlant(21, 12, shared);
    const Plant spreading = grownPlant(21, 12, plagiotropic);

    // The trunk is unaffected; the branches lie down.
    CHECK(meanInclination(spreading.graph, 1) < meanInclination(upright.graph, 1) - 5.0f);
    CHECK(meanInclination(spreading.graph, 0) ==
          doctest::Approx(meanInclination(upright.graph, 0)).epsilon(0.05));
}

TEST_CASE("branch vigour falloff keeps branches subordinate to the leader") {
    GrowthParameters equals = treeParameters();
    equals.branchVigorFalloff = 1.0f;
    GrowthParameters subordinate = equals;
    subordinate.branchVigorFalloff = 0.6f;

    const auto meanLength = [](const StemGraph& graph, std::uint16_t order) {
        float sum = 0.0f;
        std::size_t counted = 0;
        for (const Internode& node : graph.internodes()) {
            if (node.alive && node.branchOrder == order) {
                sum += node.length;
                ++counted;
            }
        }
        return counted > 0 ? sum / static_cast<float>(counted) : 0.0f;
    };

    const Plant plant = grownPlant(21, 12, subordinate);
    CHECK(meanLength(plant.graph, 1) < meanLength(plant.graph, 0));

    const Plant flat = grownPlant(21, 12, equals);
    CHECK(meanLength(flat.graph, 1) == doctest::Approx(meanLength(flat.graph, 0)).epsilon(0.2));
}

TEST_CASE("apical dominance stops a shoot from branching along its own new growth") {
    GrowthParameters free = treeParameters();
    free.apicalDominanceRange = 0.0f;
    GrowthParameters dominant = free;
    dominant.apicalDominanceRange = 1.0f;

    const Plant branchy = grownPlant(13, 10, free);
    const Plant leadered = grownPlant(13, 10, dominant);

    const auto lateralAxes = [](const StemGraph& graph) {
        std::size_t count = 0;
        for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
            const Internode& node = graph.internode(i);
            if (node.alive && node.parent != kInvalidIndex &&
                node.branchOrder == graph.internode(node.parent).branchOrder + 1) {
                ++count;
            }
        }
        return count;
    };
    CHECK(lateralAxes(leadered.graph) < lateralAxes(branchy.graph));
    // The leader itself is not held back.
    CHECK(liveOrderZeroCount(leadered.graph) >= liveOrderZeroCount(branchy.graph));
}

TEST_CASE("apical dominance releases when the apex stops extending") {
    // Dominance reaching the whole plant: while leaders keep extending, no
    // lateral may break out. Once they hit the hydraulic ceiling and stall,
    // the buds below them are free -- an excurrent sapling turning into a
    // decurrent crown.
    GrowthParameters extending = treeParameters();
    extending.apicalDominanceRange = 100.0f;
    extending.maxPathLength = 40.0f;
    GrowthParameters stalling = extending;
    stalling.maxPathLength = 3.0f;

    const auto lateralAxes = [](const StemGraph& graph) {
        std::size_t count = 0;
        for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
            const Internode& node = graph.internode(i);
            if (node.alive && node.parent != kInvalidIndex &&
                node.branchOrder == graph.internode(node.parent).branchOrder + 1) {
                ++count;
            }
        }
        return count;
    };

    CHECK(lateralAxes(grownPlant(13, 16, extending).graph) == 0);
    CHECK(lateralAxes(grownPlant(13, 16, stalling).graph) > 0);
}

TEST_CASE("allocation splits main against laterals as a group, not one by one") {
    // Borchert-Honda: the leader's share must not shrink just because it is
    // outnumbered. Many laterals on one node must not starve the continuation.
    GrowthParameters parameters = treeParameters();
    parameters.apicalControl = 0.8f;
    parameters.lateralBudsPerMetamer = 1;
    GrowthParameters crowded = parameters;
    crowded.lateralBudsPerMetamer = 4; // four times the lateral outlets

    const auto trunkLength = [](const StemGraph& graph) {
        float length = 0.0f;
        for (const Internode& node : graph.internodes()) {
            if (node.alive && node.branchOrder == 0) {
                length += node.length;
            }
        }
        return length;
    };

    const Plant sparse = grownPlant(17, 12, parameters);
    const Plant dense = grownPlant(17, 12, crowded);
    CHECK(trunkLength(dense.graph) > 0.6f * trunkLength(sparse.graph));
}

TEST_CASE("column shade reaches the ground under a canopy, pyramid shade does not") {
    // A canopy patch 10 m up, and a probe far below it.
    std::vector<Vec3> canopy;
    for (int x = -4; x <= 4; ++x) {
        for (int y = -4; y <= 4; ++y) {
            canopy.push_back(Vec3{static_cast<float>(x) * 0.5f, static_cast<float>(y) * 0.5f,
                                  10.0f});
        }
    }
    canopy.push_back(Vec3{0.0f, 0.0f, 1.0f}); // keeps the probe inside the grid

    ShadowVoxelGrid::Settings pyramid;
    pyramid.shadowDepth = 5; // reaches 2.5 m at 0.5 m voxels
    ShadowVoxelGrid::Settings column;
    column.mode = ShadowVoxelGrid::Mode::VerticalColumn;

    ShadowVoxelGrid pyramidGrid;
    ShadowVoxelGrid columnGrid;
    pyramidGrid.build(canopy, pyramid);
    columnGrid.build(canopy, column);

    const Vec3 probe{0.0f, 0.0f, 2.0f}; // 8 m below the canopy
    CHECK(pyramidGrid.shadowAt(probe) == doctest::Approx(0.0f));
    CHECK(columnGrid.shadowAt(probe) > 0.02f);
}

TEST_CASE("column shading lifts the crown") {
    GrowthParameters open = treeParameters();
    open.shadeModel = ShadeModel::VerticalExtinction;
    open.shadowIntensity = 0.004f;
    GrowthParameters dense = open;
    dense.shadowIntensity = 0.05f; // denser foliage, deeper shade below it

    const Plant underPyramid = grownPlant(8, 16, open);
    const Plant underColumn = grownPlant(8, 16, dense);

    // The lowest live twig sits higher when the canopy above it is denser.
    const auto lowestTwig = [](const StemGraph& graph, float maxRadius) {
        std::vector<Vec3> tips;
        graph.computeTipPositions(tips);
        float lowest = 1.0e30f;
        for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
            const Internode& node = graph.internode(i);
            if (node.alive && node.radius <= maxRadius) {
                lowest = std::min(lowest, tips[i].z);
            }
        }
        return lowest;
    };
    CHECK(lowestTwig(underColumn.graph, 0.02f) >= lowestTwig(underPyramid.graph, 0.02f));
}

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "flora/growth/simulation.hpp"

using namespace flora;

namespace {

Plant grownPlant(std::uint64_t seed, std::uint32_t steps, const GrowthParameters& parameters) {
    Plant plant;
    plant.seed = seed;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{}, steps, context);
    return plant;
}

float plantHeight(const StemGraph& graph) {
    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);
    float height = 0.0f;
    for (const Vec3& tip : tips) {
        height = std::max(height, tip.z);
    }
    return height;
}

std::size_t liveBudCount(const StemGraph& graph) {
    std::size_t count = 0;
    for (const Bud& bud : graph.buds()) {
        if (bud.state != BudState::Dead) {
            ++count;
        }
    }
    return count;
}

// Reference conditions for the height tests: shading off, so the only limit on
// extension is the hydraulic one.
GrowthParameters unshadedParameters(float maxPathLength) {
    GrowthParameters parameters;
    parameters.shadeModel = ShadeModel::Off;
    parameters.maxPathLength = maxPathLength;
    return parameters;
}

} // namespace

TEST_CASE("hydraulic limit: height stays under the path ceiling") {
    const GrowthParameters parameters = unshadedParameters(6.0f);
    const Plant plant = grownPlant(3, 14, parameters);
    CHECK(plantHeight(plant.graph) < parameters.maxPathLength);
}

TEST_CASE("hydraulic limit: extra steps do not keep raising the crown") {
    const GrowthParameters parameters = unshadedParameters(6.0f);
    const float early = plantHeight(grownPlant(3, 12, parameters).graph);
    const float late = plantHeight(grownPlant(3, 24, parameters).graph);
    CHECK(late >= early);
    CHECK(late - early < 0.1f * parameters.maxPathLength);
    CHECK(late < parameters.maxPathLength);
}

TEST_CASE("hydraulic limit: a shorter ceiling makes a shorter tree") {
    const float shortTree = plantHeight(grownPlant(7, 16, unshadedParameters(4.0f)).graph);
    const float tallTree = plantHeight(grownPlant(7, 16, unshadedParameters(12.0f)).graph);
    CHECK(shortTree < tallTree);
}

TEST_CASE("hydraulic limit: zero disables it") {
    const float limited = plantHeight(grownPlant(7, 16, unshadedParameters(4.0f)).graph);
    const float unlimited = plantHeight(grownPlant(7, 16, unshadedParameters(0.0f)).graph);
    CHECK(unlimited > limited);
}

TEST_CASE("hydraulic limit: metamers shorten with stem path length") {
    const Plant plant = grownPlant(5, 14, unshadedParameters(6.0f));
    const StemGraph& graph = plant.graph;

    std::vector<float> pathLength(graph.internodeCount(), 0.0f);
    float nearBase = 0.0f;
    float nearLimit = 0.0f;
    std::size_t nearBaseCount = 0;
    std::size_t nearLimitCount = 0;
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        const float upstream = node.parent == kInvalidIndex ? 0.0f : pathLength[node.parent];
        pathLength[i] = upstream + node.length;
        if (upstream < 1.0f) {
            nearBase += node.length;
            ++nearBaseCount;
        } else if (upstream > 4.5f) {
            nearLimit += node.length;
            ++nearLimitCount;
        }
    }
    REQUIRE(nearBaseCount > 0);
    REQUIRE(nearLimitCount > 0);
    CHECK(nearLimit / static_cast<float>(nearLimitCount) <
          nearBase / static_cast<float>(nearBaseCount));
}

TEST_CASE("hydraulic limit: no degenerate metamers at the ceiling") {
    const Plant plant = grownPlant(5, 20, unshadedParameters(5.0f));
    for (const Internode& node : plant.graph.internodes()) {
        CHECK(node.length > 0.0f);
    }
}

TEST_CASE("apical meristems survive dormancy so a stalled crown keeps growing points") {
    // Well past the ceiling: every axis has stopped extending for many steps.
    const Plant plant = grownPlant(9, 30, unshadedParameters(3.0f));
    CHECK(liveBudCount(plant.graph) > 0);

    std::size_t liveApical = 0;
    for (const Bud& bud : plant.graph.buds()) {
        if (bud.state != BudState::Dead && bud.type == BudType::Apical) {
            ++liveApical;
        }
    }
    CHECK(liveApical > 0);
}

TEST_CASE("sapwood shell: heartwood is free, so a thicker core costs no more") {
    // Same tree, same maintenance rate: with a thin living shell the trunk's
    // heartwood stops counting toward maintenance, so less structure is shed.
    GrowthParameters parameters;
    parameters.shadeModel = ShadeModel::Off;
    parameters.maxPathLength = 8.0f;
    parameters.maintenanceCostPerVolume = 8.0e4f;
    parameters.thicknessGrowthFactor = 5.0e-5f;

    GrowthParameters thinShell = parameters;
    thinShell.sapwoodDepth = 0.01f;
    GrowthParameters solidWood = parameters;
    solidWood.sapwoodDepth = 100.0f; // shell thicker than any radius: whole volume lives

    const Plant cheap = grownPlant(4, 16, thinShell);
    const Plant expensive = grownPlant(4, 16, solidWood);
    CHECK(cheap.graph.internodeCount() > expensive.graph.internodeCount());
}

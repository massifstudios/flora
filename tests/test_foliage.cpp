#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "flora/geometry/foliage_mesh.hpp"
#include "flora/growth/simulation.hpp"
#include "flora/structure/foliage.hpp"

using namespace flora;

namespace {

// Vertical axis of thin internodes: every one is a leaf-bearing candidate.
StemGraph thinAxis(std::uint32_t count, float length, float radius) {
    StemGraph graph;
    Index tip = graph.addRoot(kUpAxis, length, radius, 0);
    for (std::uint32_t i = 1; i < count; ++i) {
        tip = graph.addInternode(tip, AxisRelation::Continuation, kUpAxis, length, radius, i);
    }
    return graph;
}

std::vector<FoliageCluster> placedOn(const StemGraph& graph, const GrowthParameters& parameters) {
    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);
    std::vector<FoliageCluster> clusters;
    computeFoliagePlacement(graph, parameters, tips, clusters);
    return clusters;
}

} // namespace

TEST_CASE("foliage grows on thin twigs, never on thick wood") {
    GrowthParameters parameters;
    parameters.leafMaxBearingRadius = 0.02f;

    StemGraph graph;
    const Index trunk = graph.addRoot(kUpAxis, 1.0f, 0.2f, 0); // far too thick
    graph.addInternode(trunk, AxisRelation::Lateral, Vec3{1.0f, 0.0f, 0.0f}, 0.5f, 0.01f, 1);

    const auto clusters = placedOn(graph, parameters);
    REQUIRE(clusters.size() == 1);
    CHECK(clusters[0].internode == 1);
    CHECK(clusters[0].scale == parameters.leafClusterScale);
}

TEST_CASE("cluster spacing is respected along the axis path") {
    GrowthParameters parameters;
    parameters.leafClusterSpacing = 0.9f;

    const StemGraph graph = thinAxis(10, 0.5f, 0.01f); // 0.5 m per internode
    const auto clusters = placedOn(graph, parameters);

    // Spacing 0.9 over 0.5 m internodes: every second internode bears.
    CHECK(clusters.size() == 5);
    for (std::size_t i = 1; i < clusters.size(); ++i) {
        const float gap = clusters[i].position.z - clusters[i - 1].position.z;
        CHECK(gap >= parameters.leafClusterSpacing - 1.0e-4f);
    }
}

TEST_CASE("dead internodes bear no foliage") {
    GrowthParameters parameters;
    StemGraph graph = thinAxis(4, 0.5f, 0.01f);
    graph.markSubtreeDead(2, 1);
    graph.sweepDeadBuds();

    const auto clusters = placedOn(graph, parameters);
    for (const FoliageCluster& cluster : clusters) {
        CHECK(graph.internode(cluster.internode).alive);
    }
}

TEST_CASE("droop tilts cluster orientation downward") {
    GrowthParameters flat;
    flat.leafDroop = 0.0f;
    GrowthParameters drooping;
    drooping.leafDroop = 0.8f;

    StemGraph graph;
    graph.addRoot(Vec3{1.0f, 0.0f, 0.0f}, 0.5f, 0.01f, 0); // horizontal twig

    const auto level = placedOn(graph, flat);
    const auto drooped = placedOn(graph, drooping);
    REQUIRE(level.size() == 1);
    REQUIRE(drooped.size() == 1);
    CHECK(drooped[0].direction.z < level[0].direction.z - 0.1f);
}

TEST_CASE("foliage placement is deterministic") {
    GrowthParameters parameters;
    Plant plant;
    plant.seed = 77;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{}, 12, context);

    const auto first = placedOn(plant.graph, parameters);
    const auto second = placedOn(plant.graph, parameters);
    REQUIRE(first.size() == second.size());
    CHECK(first.size() > 0);
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].internode == second[i].internode);
        CHECK(first[i].position == second[i].position);
        CHECK(first[i].direction == second[i].direction);
    }
}

TEST_CASE("foliage-borne energy: a leafless plant cannot grow") {
    GrowthParameters parameters;
    parameters.shadeModel = ShadeModel::Off;
    parameters.leafMaxBearingRadius = 1.0e-6f; // nothing qualifies as leafy

    Plant plant;
    plant.seed = 3;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{}, 8, context);
    CHECK(plant.graph.internodeCount() == 1);
}

TEST_CASE("foliage mesh: expected card counts, valid and deterministic") {
    GrowthParameters growthParameters;
    Plant plant;
    plant.seed = 15;
    plantSeedling(plant, growthParameters);
    GrowthContext context;
    grow(plant, growthParameters, ConstantEnvironment{}, 12, context);
    const auto clusters = placedOn(plant.graph, growthParameters);
    REQUIRE(clusters.size() > 0);

    FoliageMeshParameters meshParameters;
    meshParameters.quadsPerCluster = 2;
    meshParameters.seed = plant.seed;
    Mesh mesh;
    generateFoliageMesh(clusters, meshParameters, mesh);

    CHECK(mesh.vertexCount() == clusters.size() * 2 * 4);
    CHECK(mesh.triangleCount() == clusters.size() * 2 * 2);
    std::string errors;
    CHECK_MESSAGE(validateMesh(mesh, &errors), errors);

    Mesh again;
    generateFoliageMesh(clusters, meshParameters, again);
    REQUIRE(again.vertexCount() == mesh.vertexCount());
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        CHECK(mesh.positions[i] == again.positions[i]);
    }
}

TEST_CASE("leaf-shade coupling: denser foliage casts more shade and slows growth") {
    GrowthParameters sparse;
    sparse.shadowIntensity = 0.05f;
    GrowthParameters dense = sparse;
    dense.shadowIntensity = 0.6f;

    Plant sparsePlant;
    sparsePlant.seed = 8;
    plantSeedling(sparsePlant, sparse);
    Plant densePlant;
    densePlant.seed = 8;
    plantSeedling(densePlant, dense);
    GrowthContext context;
    grow(sparsePlant, sparse, ConstantEnvironment{}, 14, context);
    grow(densePlant, dense, ConstantEnvironment{}, 14, context);

    CHECK(densePlant.graph.internodeCount() < sparsePlant.graph.internodeCount());
}

TEST_CASE("foliage sits near tips, not on old wood behind them") {
    GrowthParameters parameters;
    parameters.leafBearingTipDistance = 1.0f;
    parameters.leafClusterSpacing = 0.0f; // isolate the tip-distance rule
    parameters.leafMaxBearingRadius = 1.0f;

    const StemGraph graph = thinAxis(10, 0.5f, 0.01f); // 5 m axis, tip at index 9
    const std::vector<FoliageCluster> clusters = placedOn(graph, parameters);

    REQUIRE(!clusters.empty());
    for (const FoliageCluster& cluster : clusters) {
        CHECK(cluster.internode >= 7); // within 1 m of the tip
    }
}

TEST_CASE("tip distance is measured to the nearest tip, not through one branch") {
    GrowthParameters parameters;
    parameters.leafBearingTipDistance = 0.6f;
    parameters.leafClusterSpacing = 0.0f;
    parameters.leafMaxBearingRadius = 1.0f;

    // A fork whose two arms differ in length: the short arm brings a tip close
    // to the junction, so the junction bears leaves.
    StemGraph graph;
    const Index junction = graph.addRoot(kUpAxis, 0.5f, 0.01f, 0);
    graph.addInternode(junction, AxisRelation::Continuation, kUpAxis, 0.5f, 0.01f, 0);
    Index longArm = graph.addInternode(junction, AxisRelation::Lateral, Vec3{1, 0, 0}, 0.5f, 0.01f, 0);
    for (int i = 0; i < 4; ++i) {
        longArm = graph.addInternode(longArm, AxisRelation::Continuation, Vec3{1, 0, 0}, 0.5f,
                                     0.01f, 0);
    }

    const std::vector<FoliageCluster> clusters = placedOn(graph, parameters);
    bool junctionBears = false;
    for (const FoliageCluster& cluster : clusters) {
        junctionBears = junctionBears || cluster.internode == junction;
    }
    CHECK(junctionBears);
}

TEST_CASE("thickening does not move the leaf-bearing zone") {
    // The point of keying foliage to tip distance: stems thicken over a tree's
    // life, and that must not decide where leaves live.
    GrowthParameters parameters;
    parameters.leafBearingTipDistance = 1.0f;
    parameters.leafClusterSpacing = 0.0f;
    parameters.leafMaxBearingRadius = 1.0f;

    const StemGraph thin = thinAxis(10, 0.5f, 0.005f);
    const StemGraph thick = thinAxis(10, 0.5f, 0.4f);
    CHECK(placedOn(thin, parameters).size() == placedOn(thick, parameters).size());
}

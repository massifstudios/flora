#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "flora/geometry/branch_mesh.hpp"
#include "flora/growth/simulation.hpp"

using namespace flora;

namespace {

StemGraph singleAxis(std::uint32_t internodeCount, float radius) {
    StemGraph graph;
    Index tip = graph.addRoot(kUpAxis, 0.5f, radius, 0);
    for (std::uint32_t i = 1; i < internodeCount; ++i) {
        tip = graph.addInternode(tip, AxisRelation::Continuation, kUpAxis, 0.5f, radius, i);
    }
    return graph;
}

Plant grownPlant(std::uint64_t seed, std::uint32_t steps) {
    Plant plant;
    plant.seed = seed;
    GrowthParameters parameters;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{}, steps, context);
    return plant;
}

} // namespace

TEST_CASE("empty graph produces an empty mesh") {
    StemGraph graph;
    Mesh mesh;
    generateBranchMesh(graph, BranchMeshParameters{}, mesh);
    CHECK(mesh.vertexCount() == 0);
    CHECK(mesh.triangleCount() == 0);
}

TEST_CASE("single axis produces the expected vertex and triangle counts") {
    const StemGraph graph = singleAxis(4, 0.2f); // thick: full radial segments
    BranchMeshParameters parameters;
    parameters.radialSegments = 8;
    Mesh mesh;
    generateBranchMesh(graph, parameters, mesh);

    // 5 rings of (8+1) seam-duplicated vertices plus the tip-cap center.
    CHECK(mesh.vertexCount() == 5 * 9 + 1);
    // 4 quad bands of 8 quads = 64 triangles, plus 8 cap triangles.
    CHECK(mesh.triangleCount() == 64 + 8);

    std::string errors;
    CHECK_MESSAGE(validateMesh(mesh, &errors), errors);
}

TEST_CASE("mesh from a grown plant is valid and finite") {
    const Plant plant = grownPlant(42, 12);
    Mesh mesh;
    generateBranchMesh(plant.graph, BranchMeshParameters{}, mesh);

    CHECK(mesh.triangleCount() > 1000);
    std::string errors;
    CHECK_MESSAGE(validateMesh(mesh, &errors), errors);
}

TEST_CASE("mesh generation is deterministic") {
    const Plant plant = grownPlant(7, 10);
    Mesh first;
    Mesh second;
    generateBranchMesh(plant.graph, BranchMeshParameters{}, first);
    generateBranchMesh(plant.graph, BranchMeshParameters{}, second);

    REQUIRE(first.vertexCount() == second.vertexCount());
    REQUIRE(first.indices.size() == second.indices.size());
    for (std::size_t i = 0; i < first.positions.size(); ++i) {
        CHECK(first.positions[i] == second.positions[i]);
        CHECK(first.normals[i] == second.normals[i]);
        CHECK(first.uvs[i] == second.uvs[i]);
    }
    for (std::size_t i = 0; i < first.indices.size(); ++i) {
        CHECK(first.indices[i] == second.indices[i]);
    }
}

TEST_CASE("mesh bounds contain the structure and respect the radius margin") {
    const StemGraph graph = singleAxis(6, 0.1f);
    Mesh mesh;
    generateBranchMesh(graph, BranchMeshParameters{}, mesh);

    Vec3 minimum;
    Vec3 maximum;
    REQUIRE(computeBounds(mesh, minimum, maximum));
    CHECK(maximum.z == doctest::Approx(3.0f).epsilon(0.001)); // 6 * 0.5 m
    CHECK(minimum.z == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(maximum.x <= 0.11f); // radius plus tolerance
    CHECK(minimum.x >= -0.11f);
}

TEST_CASE("thin axes are decimated to fewer radial segments") {
    BranchMeshParameters parameters;
    parameters.radialSegments = 12;
    parameters.minimumRadialSegments = 3;
    parameters.decimationRadiusMin = 0.02f;
    parameters.decimationRadiusMax = 0.2f;

    Mesh thick;
    generateBranchMesh(singleAxis(3, 0.25f), parameters, thick);
    Mesh thin;
    generateBranchMesh(singleAxis(3, 0.01f), parameters, thin);

    // Same ring count, fewer vertices per ring on the thin axis.
    CHECK(thin.vertexCount() < thick.vertexCount());
    CHECK(thin.vertexCount() == 4 * (3 + 1) + 1); // minimum segments
    CHECK(thick.vertexCount() == 4 * (12 + 1) + 1);
}

TEST_CASE("pruned subtrees produce no geometry") {
    Plant plant = grownPlant(9, 10);
    Mesh before;
    generateBranchMesh(plant.graph, BranchMeshParameters{}, before);

    plant.graph.markSubtreeDead(1);
    plant.graph.sweepDeadBuds();
    Mesh after;
    generateBranchMesh(plant.graph, BranchMeshParameters{}, after);

    CHECK(after.vertexCount() < before.vertexCount());
    std::string errors;
    CHECK_MESSAGE(validateMesh(after, &errors), errors);
}

TEST_CASE("u coordinates stay within the wrap interval") {
    const Plant plant = grownPlant(3, 8);
    Mesh mesh;
    generateBranchMesh(plant.graph, BranchMeshParameters{}, mesh);
    for (const Vec2& uv : mesh.uvs) {
        CHECK(uv.x >= 0.0f);
        CHECK(uv.x <= 1.0f);
        CHECK(uv.y >= 0.0f);
    }
}

TEST_CASE("pipe-model display radii thicken the trunk") {
    const Plant plant = grownPlant(11, 12);

    BranchMeshParameters stored;
    Mesh storedMesh;
    generateBranchMesh(plant.graph, stored, storedMesh);

    BranchMeshParameters display;
    display.radiusSource = RadiusSource::PipeModelDisplay;
    Mesh displayMesh;
    generateBranchMesh(plant.graph, display, displayMesh);

    // Trunk cross-section grows with downstream count: compare the widest
    // ring at the base (z near ground), where the whole tree is downstream.
    const auto baseRingRadius = [](const Mesh& mesh) {
        float widest = 0.0f;
        for (const Vec3& position : mesh.positions) {
            if (position.z < 0.01f) {
                widest = std::max(widest,
                                  std::sqrt(position.x * position.x + position.y * position.y));
            }
        }
        return widest;
    };
    CHECK(baseRingRadius(displayMesh) > 5.0f * baseRingRadius(storedMesh));

    std::string errors;
    CHECK_MESSAGE(validateMesh(displayMesh, &errors), errors);
}

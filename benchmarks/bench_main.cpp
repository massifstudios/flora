#include <cstdio>
#include <vector>

#include "bench.hpp"
#include "flora/geometry/branch_mesh.hpp"
#include "flora/geometry/foliage_mesh.hpp"
#include "flora/growth/simulation.hpp"
#include "flora/structure/foliage.hpp"
#include "flora/rng/random.hpp"
#include "flora/structure/stem_graph.hpp"
#include "flora/version.hpp"

using namespace flora;

namespace {

// Same synthetic shape as the unit tests, scaled up: main axis with one
// lateral + bud per internode. Deterministic, allocation pattern matches
// realistic growth (interleaved axis extension and lateral creation).
StemGraph buildSyntheticTree(std::uint32_t axisLength) {
    StemGraph graph;
    graph.reserve(static_cast<std::size_t>(axisLength) * 2 + 1, axisLength + 1);
    Index tip = graph.addRoot(kUpAxis, 0.5f, 0.05f, 0);
    for (std::uint32_t step = 1; step <= axisLength; ++step) {
        const Index lateral = graph.addInternode(
            tip, AxisRelation::Lateral, Vec3{1.0f, 0.0f, 0.5f}.normalizedOr(kUpAxis), 0.3f, 0.02f, step);
        graph.addBud(lateral, BudType::Apical, kUpAxis, step);
        tip = graph.addInternode(tip, AxisRelation::Continuation, kUpAxis, 0.5f, 0.05f, step);
    }
    return graph;
}

} // namespace

int main() {
    std::printf("flora benchmarks — version %u.%u.%u, simulation revision %u\n", kVersionMajor,
                kVersionMinor, kVersionPatch, kSimulationRevision);

    bench::Registry registry;

    constexpr std::uint32_t kAxisLength = 50000;
    constexpr std::uint64_t kInternodeCount = static_cast<std::uint64_t>(kAxisLength) * 2 + 1;

    registry.run("stem_graph/build_100k_internodes", kInternodeCount, "internode", [&] {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        if (graph.internodeCount() != kInternodeCount) {
            std::printf("unexpected internode count\n");
        }
    });

    {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        std::vector<Vec3> tips;
        registry.run("stem_graph/compute_tip_positions_100k", kInternodeCount, "internode",
                     [&] { graph.computeTipPositions(tips); });
    }

    registry.run("stem_graph/prune_half_and_compact_100k", kInternodeCount, "internode", [&] {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        graph.markSubtreeDead(kAxisLength); // roughly the upper half of the axis
        graph.sweepDeadBuds();
        graph.compact();
    });

    {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        std::string errors;
        registry.run("stem_graph/validate_100k", kInternodeCount, "internode", [&] {
            if (!graph.validate(&errors)) {
                std::printf("validate failed: %s\n", errors.c_str());
            }
        });
    }

    constexpr std::uint64_t kRngDraws = 10'000'000;
    registry.run("rng/next_uint64_10m", kRngDraws, "draw", [&] {
        auto stream = RandomStream::derive(1, RngPurpose::Test, 0);
        std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < kRngDraws; ++i) {
            sink ^= stream.nextUInt64();
        }
        if (sink == 0) {
            std::printf("unlikely\n");
        }
    });

    {
        // Single tree grown to the internode cap (~35 steps, shading off:
        // the reference path keeps this comparable across revisions).
        GrowthParameters parameters;
        parameters.shadeModel = ShadeModel::Off;
        std::uint64_t grownInternodes = 0;
        GrowthContext context;
        registry.run("growth/single_tree_to_100k_internodes", 100000, "internode", [&] {
            Plant plant;
            plant.seed = 42;
            plantSeedling(plant, parameters);
            grow(plant, parameters, ConstantEnvironment{}, 40, context);
            grownInternodes = plant.graph.internodeCount();
        });
        std::printf("  (single tree reached %llu internodes in 40 steps)\n",
                    static_cast<unsigned long long>(grownInternodes));
    }

    {
        // Same tree under voxel self-shading: light competition caps the
        // structure well below the internode limit.
        GrowthParameters parameters;
        std::uint64_t grownInternodes = 0;
        GrowthContext context;
        registry.run("growth/single_tree_voxel_shade_40_steps", 40, "step", [&] {
            Plant plant;
            plant.seed = 42;
            plantSeedling(plant, parameters);
            grow(plant, parameters, ConstantEnvironment{}, 40, context);
            grownInternodes = plant.graph.internodeCount();
        });
        std::printf("  (voxel-shaded tree reached %llu internodes in 40 steps)\n",
                    static_cast<unsigned long long>(grownInternodes));
    }

    {
        // Sapling batch: many small plants, the forest-simulation shape.
        GrowthParameters parameters;
        constexpr std::uint64_t kSaplings = 1000;
        GrowthContext context;
        std::uint64_t totalInternodes = 0;
        registry.run("growth/1000_saplings_15_steps", kSaplings, "plant", [&] {
            totalInternodes = 0;
            for (std::uint64_t i = 0; i < kSaplings; ++i) {
                Plant plant;
                plant.seed = 1000 + i;
                plantSeedling(plant, parameters);
                grow(plant, parameters, ConstantEnvironment{}, 15, context);
                totalInternodes += plant.graph.internodeCount();
            }
        });
        std::printf("  (saplings total %llu internodes)\n",
                    static_cast<unsigned long long>(totalInternodes));
    }

    {
        // Light backends on a synthetic 20k-caster crown (~20 m across).
        constexpr std::uint64_t kCasters = 20000;
        std::vector<Vec3> casters;
        casters.reserve(kCasters);
        auto stream = RandomStream::derive(9, RngPurpose::Test, 1);
        for (std::uint64_t i = 0; i < kCasters; ++i) {
            casters.push_back(Vec3{stream.uniformFloat(-10.0f, 10.0f),
                                   stream.uniformFloat(-10.0f, 10.0f),
                                   stream.uniformFloat(5.0f, 25.0f)});
        }
        ShadowVoxelGrid grid;
        ShadowVoxelGrid::Settings settings;
        registry.run("light/voxel_grid_build_20k_casters", kCasters, "caster",
                     [&] { grid.build(casters, settings); });

        float sink = 0.0f;
        registry.run("light/voxel_shadow_query_20k", kCasters, "query", [&] {
            sink = 0.0f;
            for (const Vec3& position : casters) {
                sink += grid.shadowAt(position);
            }
        });

        ShadowVoxelGrid::Settings occupancy;
        occupancy.shadowDepth = 0;
        ShadowVoxelGrid rayGrid;
        rayGrid.build(casters, occupancy);
        constexpr std::uint64_t kRayQueries = 1000;
        constexpr std::uint32_t kRaysPerQuery = 32;
        registry.run("light/raycast_query_1k_x32rays", kRayQueries * kRaysPerQuery, "ray", [&] {
            sink = 0.0f;
            for (std::uint64_t i = 0; i < kRayQueries; ++i) {
                const Vec3& position = casters[i * (kCasters / kRayQueries)];
                for (std::uint32_t ray = 0; ray < kRaysPerQuery; ++ray) {
                    const float upBias =
                        (static_cast<float>(ray) + 0.5f) / static_cast<float>(kRaysPerQuery);
                    const Vec3 direction =
                        Vec3{1.0f - upBias, 0.3f, upBias}.normalizedOr(kUpAxis);
                    sink += rayGrid.transmittanceAlong(position, direction, 1.0f);
                }
            }
        });
        if (sink < 0.0f) {
            std::printf("unlikely\n");
        }
    }

    {
        // Geometry from a full-size grown tree, both radius modes.
        GrowthParameters growthParameters;
        growthParameters.shadeModel = ShadeModel::Off; // full 100k-internode mesh workload
        Plant plant;
        plant.seed = 42;
        plantSeedling(plant, growthParameters);
        GrowthContext context;
        grow(plant, growthParameters, ConstantEnvironment{}, 40, context);
        const std::uint64_t internodes = plant.graph.internodeCount();

        Mesh mesh;
        BranchMeshParameters stored;
        registry.run("geometry/branch_mesh_100k_stored", internodes, "internode",
                     [&] { generateBranchMesh(plant.graph, stored, mesh); });
        std::printf("  (stored radii: %zu vertices, %zu triangles)\n", mesh.vertexCount(),
                    mesh.triangleCount());

        BranchMeshParameters display;
        display.radiusSource = RadiusSource::PipeModelDisplay;
        registry.run("geometry/branch_mesh_100k_pipe_display", internodes, "internode",
                     [&] { generateBranchMesh(plant.graph, display, mesh); });
        std::printf("  (display radii: %zu vertices, %zu triangles)\n", mesh.vertexCount(),
                    mesh.triangleCount());

        std::vector<Vec3> tips;
        plant.graph.computeTipPositions(tips);
        std::vector<float> scratch;
        std::vector<FoliageCluster> clusters;
        registry.run("foliage/placement_100k_internodes", internodes, "internode", [&] {
            computeFoliagePlacement(plant.graph, growthParameters, tips, scratch, clusters);
        });
        std::printf("  (placed %zu clusters)\n", clusters.size());

        FoliageMeshParameters foliageParameters;
        foliageParameters.seed = plant.seed;
        Mesh leaves;
        registry.run("foliage/mesh_generation", clusters.size(), "cluster",
                     [&] { generateFoliageMesh(clusters, foliageParameters, leaves); });
        std::printf("  (foliage: %zu vertices, %zu triangles)\n", leaves.vertexCount(),
                    leaves.triangleCount());
    }

    registry.report();

    const double bytesPerInternode = static_cast<double>(sizeof(Internode));
    const double bytesPerBud = static_cast<double>(sizeof(Bud));
    std::printf("\nmemory: sizeof(Internode) = %.0f B, sizeof(Bud) = %.0f B\n", bytesPerInternode,
                bytesPerBud);
    std::printf("100k-internode tree structure: %.2f MB (+%.2f MB buds)\n",
                bytesPerInternode * static_cast<double>(kInternodeCount) / 1.0e6,
                bytesPerBud * static_cast<double>(kAxisLength + 1) / 1.0e6);
    return 0;
}

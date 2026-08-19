#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "flora/geometry/branch_mesh.hpp"
#include "flora/geometry/foliage_mesh.hpp"
#include "flora/growth/simulation.hpp"
#include "flora/structure/foliage.hpp"
#include "flora/species/species.hpp"
#include "flora/species/species_io.hpp"
#include "flora/structure/stem_graph.hpp"
#include "flora/version.hpp"

using namespace flora;

namespace {

// Skeleton preview: OBJ line elements, one polyline segment per internode.
bool writeSkeletonObj(const StemGraph& graph, const char* path) {
    std::FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);

    std::fprintf(file, "# flora skeleton preview (Z up, meters)\n");
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        const Vec3 base = node.parent == kInvalidIndex ? Vec3{} : tips[node.parent];
        const Vec3& tip = tips[i];
        std::fprintf(file, "v %.4f %.4f %.4f\nv %.4f %.4f %.4f\n", static_cast<double>(base.x),
                     static_cast<double>(base.y), static_cast<double>(base.z),
                     static_cast<double>(tip.x), static_cast<double>(tip.y),
                     static_cast<double>(tip.z));
        const long vertexBase = static_cast<long>(i) * 2 + 1;
        std::fprintf(file, "l %ld %ld\n", vertexBase, vertexBase + 1);
    }
    std::fclose(file);
    return true;
}

void writeMeshGroup(std::FILE* file, const Mesh& mesh, const char* group,
                    unsigned long indexOffset) {
    std::fprintf(file, "g %s\nusemtl %s\n", group, group);
    for (const Vec3& p : mesh.positions) {
        std::fprintf(file, "v %.5f %.5f %.5f\n", static_cast<double>(p.x),
                     static_cast<double>(p.y), static_cast<double>(p.z));
    }
    for (const Vec2& uv : mesh.uvs) {
        std::fprintf(file, "vt %.5f %.5f\n", static_cast<double>(uv.x),
                     static_cast<double>(uv.y));
    }
    for (const Vec3& n : mesh.normals) {
        std::fprintf(file, "vn %.4f %.4f %.4f\n", static_cast<double>(n.x),
                     static_cast<double>(n.y), static_cast<double>(n.z));
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const unsigned long a = mesh.indices[i] + 1 + indexOffset;
        const unsigned long b = mesh.indices[i + 1] + 1 + indexOffset;
        const unsigned long c = mesh.indices[i + 2] + 1 + indexOffset;
        std::fprintf(file, "f %lu/%lu/%lu %lu/%lu/%lu %lu/%lu/%lu\n", a, a, a, b, b, b, c, c, c);
    }
}

// Branch + foliage mesh as one OBJ with material groups (Blender/MeshLab).
// Z up, meters.
bool writeTreeObj(const Mesh& branches, const Mesh& leaves, const char* path) {
    std::FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "# flora tree mesh (Z up, meters)\n");
    writeMeshGroup(file, branches, "bark", 0);
    if (leaves.vertexCount() > 0) {
        writeMeshGroup(file, leaves, "foliage", static_cast<unsigned long>(branches.vertexCount()));
    }
    std::fclose(file);
    return true;
}

int printUsage() {
    std::printf("flora %u.%u.%u (simulation revision %u, species schema v%u)\n", kVersionMajor,
                kVersionMinor, kVersionPatch, kSimulationRevision, kSpeciesSchemaVersion);
    std::printf("usage:\n");
    std::printf("  flora version\n");
    std::printf("  flora grow <seed> <steps> [options]    grow one plant, print stats\n");
    std::printf("options:\n");
    std::printf("    --species <file>     species definition JSON (see presets/)\n");
    std::printf("    --obj <path>         write solid branch mesh OBJ (Blender/MeshLab)\n");
    std::printf("    --skeleton <path>    write skeleton line OBJ (debug view)\n");
    std::printf("    --shade <mode>       self-shading: voxel (default), raycast, off\n");
    return 1;
}

int runGrow(std::uint64_t seed, std::uint32_t steps, const SpeciesDefinition& species,
            const char* objPath, const char* skeletonPath, ShadeModel shadeModel) {
    Plant plant;
    plant.seed = seed;
    plantSeedling(plant, materializeGrowthParameters(species, seed, 0));

    GrowthContext context;
    grow(plant, species, SkyEnvironment{}, steps, context, shadeModel);

    std::string errors;
    const bool valid = plant.graph.validate(&errors);

    std::vector<Vec3> tips;
    plant.graph.computeTipPositions(tips);
    float height = 0.0f;
    float spread = 0.0f;
    std::uint16_t maxOrder = 0;
    for (Index i = 0; i < static_cast<Index>(plant.graph.internodeCount()); ++i) {
        height = std::max(height, tips[i].z);
        spread = std::max(spread, tips[i].x * tips[i].x + tips[i].y * tips[i].y);
        maxOrder = std::max(maxOrder, plant.graph.internode(i).branchOrder);
    }

    std::size_t liveBuds = 0;
    for (const Bud& bud : plant.graph.buds()) {
        if (bud.state != BudState::Dead) {
            ++liveBuds;
        }
    }

    std::printf("species:     %s\n", species.name.c_str());
    std::printf("seed:        %llu\n", static_cast<unsigned long long>(seed));
    std::printf("age:         %u steps\n", plant.ageSteps);
    std::printf("internodes:  %zu\n", plant.graph.internodeCount());
    std::printf("buds:        %zu live / %zu total\n", liveBuds, plant.graph.budCount());
    std::printf("height:      %.2f m\n", static_cast<double>(height));
    std::printf("crown radius %.2f m\n", static_cast<double>(std::sqrt(spread)));
    std::printf("max order:   %u\n", maxOrder);

    const GrowthParameters finalParameters =
        materializeGrowthParameters(species, seed, plant.ageSteps);
    std::vector<FoliageCluster> clusters;
    computeFoliagePlacement(plant.graph, finalParameters, tips, clusters);
    std::printf("foliage:     %zu clusters\n", clusters.size());
    std::printf("invariants:  %s\n", valid ? "ok" : errors.c_str());

    if (objPath != nullptr) {
        BranchMeshParameters meshParameters; // stored radii: simulated by M5 thickening
        Mesh mesh;
        generateBranchMesh(plant.graph, meshParameters, mesh);
        FoliageMeshParameters foliageParameters;
        foliageParameters.seed = seed;
        Mesh leaves;
        generateFoliageMesh(clusters, foliageParameters, leaves);
        std::string meshErrors;
        if (!validateMesh(mesh, &meshErrors) || !validateMesh(leaves, &meshErrors)) {
            std::printf("mesh:        INVALID: %s\n", meshErrors.c_str());
            return 2;
        }
        if (writeTreeObj(mesh, leaves, objPath)) {
            std::printf("mesh:        wrote %s (%zu vertices, %zu triangles, %zu leaf cards)\n",
                        objPath, mesh.vertexCount() + leaves.vertexCount(),
                        mesh.triangleCount() + leaves.triangleCount(),
                        leaves.triangleCount() / 2);
        } else {
            std::printf("mesh:        FAILED to write %s\n", objPath);
            return 2;
        }
    }
    if (skeletonPath != nullptr) {
        if (writeSkeletonObj(plant.graph, skeletonPath)) {
            std::printf("skeleton:    wrote %s\n", skeletonPath);
        } else {
            std::printf("skeleton:    FAILED to write %s\n", skeletonPath);
            return 2;
        }
    }
    return valid ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return printUsage();
    }
    if (std::strcmp(argv[1], "version") == 0) {
        std::printf("flora %u.%u.%u (simulation revision %u, species schema v%u)\n", kVersionMajor,
                    kVersionMinor, kVersionPatch, kSimulationRevision, kSpeciesSchemaVersion);
        return 0;
    }
    if (std::strcmp(argv[1], "grow") == 0 && argc >= 4) {
        const std::uint64_t seed = std::stoull(argv[2]);
        const std::uint32_t steps = static_cast<std::uint32_t>(std::stoul(argv[3]));
        SpeciesDefinition species;
        const char* objPath = nullptr;
        const char* skeletonPath = nullptr;
        ShadeModel shadeModel = ShadeModel::VoxelPropagation;
        for (int i = 4; i < argc; ++i) {
            if (std::strcmp(argv[i], "--species") == 0 && i + 1 < argc) {
                const char* path = argv[++i];
                std::ifstream file(path);
                if (!file) {
                    std::printf("cannot open species file: %s\n", path);
                    return 1;
                }
                std::ostringstream text;
                text << file.rdbuf();
                std::string error;
                if (!loadSpeciesFromJson(text.str(), species, &error)) {
                    std::printf("invalid species file %s: %s\n", path, error.c_str());
                    return 1;
                }
            } else if (std::strcmp(argv[i], "--obj") == 0 && i + 1 < argc) {
                objPath = argv[++i];
            } else if (std::strcmp(argv[i], "--skeleton") == 0 && i + 1 < argc) {
                skeletonPath = argv[++i];
            } else if (std::strcmp(argv[i], "--shade") == 0 && i + 1 < argc) {
                const char* mode = argv[++i];
                if (std::strcmp(mode, "voxel") == 0) {
                    shadeModel = ShadeModel::VoxelPropagation;
                } else if (std::strcmp(mode, "raycast") == 0) {
                    shadeModel = ShadeModel::Raycast;
                } else if (std::strcmp(mode, "off") == 0) {
                    shadeModel = ShadeModel::Off;
                } else {
                    return printUsage();
                }
            } else {
                return printUsage();
            }
        }
        return runGrow(seed, steps, species, objPath, skeletonPath, shadeModel);
    }
    return printUsage();
}

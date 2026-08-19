#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "flora/growth/simulation.hpp"
#include "flora/rng/random.hpp"
#include "flora/structure/stem_graph.hpp"
#include "flora/version.hpp"

using namespace flora;

namespace {

struct Preset {
    const char* name;
    const char* blurb;
    GrowthParameters parameters;
};

// Built-in parameter presets demonstrating species-like variation. Replaced by
// data-driven species definitions (JSON, distributions) in M4.
std::vector<Preset> builtinPresets() {
    Preset excurrent;
    excurrent.name = "excurrent";
    excurrent.blurb = "strong apical control, narrow conifer-like habit";
    excurrent.parameters.apicalControl = 0.82f;
    excurrent.parameters.branchAngle = 1.1344640f; // 65 deg, flat whorled branches
    excurrent.parameters.lateralBudsPerMetamer = 2;
    excurrent.parameters.baseGrowthLength = 0.30f;

    Preset decurrent;
    decurrent.name = "decurrent";
    decurrent.blurb = "weak apical control, spreading broadleaf-like habit";
    decurrent.parameters.apicalControl = 0.55f;
    decurrent.parameters.branchAngle = 0.6108652f; // 35 deg
    decurrent.parameters.lateralBudsPerMetamer = 1;
    decurrent.parameters.baseGrowthLength = 0.40f;

    return {excurrent, decurrent};
}

// Skeleton preview: OBJ line elements, one polyline segment per internode.
// Openable in Blender / MeshLab. Solid meshing arrives with flora-geometry (M3).
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

int printUsage() {
    std::printf("flora %u.%u.%u (simulation revision %u)\n", kVersionMajor, kVersionMinor,
                kVersionPatch, kSimulationRevision);
    std::printf("usage:\n");
    std::printf("  flora version\n");
    std::printf("  flora presets                          list built-in parameter presets\n");
    std::printf("  flora grow <seed> <steps> [options]    grow one plant, print stats\n");
    std::printf("options:\n");
    std::printf("    --preset <name>    parameter preset (default: decurrent)\n");
    std::printf("    --obj <path>       write skeleton preview OBJ for Blender/MeshLab\n");
    return 1;
}

int runGrow(std::uint64_t seed, std::uint32_t steps, const Preset& preset, const char* objPath) {
    Plant plant;
    plant.seed = seed;
    plantSeedling(plant, preset.parameters);

    GrowthContext context;
    grow(plant, preset.parameters, ConstantEnvironment{}, steps, context);

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

    std::printf("preset:      %s (%s)\n", preset.name, preset.blurb);
    std::printf("seed:        %llu\n", static_cast<unsigned long long>(seed));
    std::printf("age:         %u steps\n", plant.ageSteps);
    std::printf("internodes:  %zu\n", plant.graph.internodeCount());
    std::printf("buds:        %zu live / %zu total\n", liveBuds, plant.graph.budCount());
    std::printf("height:      %.2f m\n", static_cast<double>(height));
    std::printf("crown radius %.2f m\n", static_cast<double>(std::sqrt(spread)));
    std::printf("max order:   %u\n", maxOrder);
    std::printf("invariants:  %s\n", valid ? "ok" : errors.c_str());

    if (objPath != nullptr) {
        if (writeSkeletonObj(plant.graph, objPath)) {
            std::printf("preview:     wrote %s\n", objPath);
        } else {
            std::printf("preview:     FAILED to write %s\n", objPath);
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
        std::printf("flora %u.%u.%u (simulation revision %u)\n", kVersionMajor, kVersionMinor,
                    kVersionPatch, kSimulationRevision);
        return 0;
    }
    const std::vector<Preset> presets = builtinPresets();
    if (std::strcmp(argv[1], "presets") == 0) {
        for (const Preset& preset : presets) {
            std::printf("%-12s %s\n", preset.name, preset.blurb);
        }
        return 0;
    }
    if (std::strcmp(argv[1], "grow") == 0 && argc >= 4) {
        const std::uint64_t seed = std::stoull(argv[2]);
        const std::uint32_t steps = static_cast<std::uint32_t>(std::stoul(argv[3]));
        const Preset* preset = &presets[1]; // decurrent default
        const char* objPath = nullptr;
        for (int i = 4; i < argc; ++i) {
            if (std::strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
                const char* name = argv[++i];
                preset = nullptr;
                for (const Preset& candidate : presets) {
                    if (std::strcmp(candidate.name, name) == 0) {
                        preset = &candidate;
                    }
                }
                if (preset == nullptr) {
                    std::printf("unknown preset: %s\n", name);
                    return 1;
                }
            } else if (std::strcmp(argv[i], "--obj") == 0 && i + 1 < argc) {
                objPath = argv[++i];
            } else {
                return printUsage();
            }
        }
        return runGrow(seed, steps, *preset, objPath);
    }
    return printUsage();
}

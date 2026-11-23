#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "flora/geometry/branch_mesh.hpp"
#include "flora/geometry/foliage_cards.hpp"
#include "flora/geometry/foliage_mesh.hpp"
#include "flora/geometry/lod.hpp"
#include "flora/geometry/material.hpp"
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

void writeMeshVertices(std::FILE* file, const Mesh& mesh) {
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
}

void writeFaceRange(std::FILE* file, const Mesh& mesh, const std::string& group,
                    unsigned long indexOffset, std::size_t firstIndex, std::size_t indexCount) {
    std::fprintf(file, "g %s\nusemtl %s\n", group.c_str(), group.c_str());
    const std::size_t end = std::min(firstIndex + indexCount, mesh.indices.size());
    for (std::size_t i = firstIndex; i + 2 < end; i += 3) {
        const unsigned long a = mesh.indices[i] + 1 + indexOffset;
        const unsigned long b = mesh.indices[i + 1] + 1 + indexOffset;
        const unsigned long c = mesh.indices[i + 2] + 1 + indexOffset;
        std::fprintf(file, "f %lu/%lu/%lu %lu/%lu/%lu %lu/%lu/%lu\n", a, a, a, b, b, b, c, c, c);
    }
}

// Branch + foliage mesh as one OBJ with material groups (Blender/MeshLab).
// Z up, meters. Foliage vertices are written once and split into one group per
// section, so a variant atlas costs face lines rather than duplicated geometry.
bool writeTreeObj(const Mesh& branches, const Mesh& leaves,
                  const std::vector<MeshSection>& leafSections,
                  const std::vector<std::string>& leafGroups, const char* path,
                  const std::string& materialLibrary) {
    std::FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "# flora tree mesh (Z up, meters)\n");
    if (!materialLibrary.empty()) {
        std::fprintf(file, "mtllib %s\n", materialLibrary.c_str());
    }
    writeMeshVertices(file, branches);
    writeFaceRange(file, branches, "bark", 0, 0, branches.indices.size());
    if (leaves.vertexCount() > 0) {
        const unsigned long offset = static_cast<unsigned long>(branches.vertexCount());
        writeMeshVertices(file, leaves);
        for (const MeshSection& section : leafSections) {
            if (section.indexCount == 0) {
                continue; // no card chose this variant
            }
            const std::string& group = section.material < leafGroups.size()
                                           ? leafGroups[section.material]
                                           : leafGroups.front();
            writeFaceRange(file, leaves, group, offset, section.firstIndex, section.indexCount);
        }
    }
    std::fclose(file);
    return true;
}

// "tree.obj" -> ("tree.mtl", "tree.mtl"); the OBJ references the library by
// file name, so both stay next to each other.
std::string materialLibraryPath(const std::string& objPath) {
    const std::size_t dot = objPath.find_last_of('.');
    const std::size_t slash = objPath.find_last_of("/\\");
    const bool hasExtension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    return (hasExtension ? objPath.substr(0, dot) : objPath) + ".mtl";
}

std::string fileNameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Texture paths in the material file resolve from the working directory; the
// MTL sits next to the OBJ, so they need re-rooting when the OBJ is elsewhere.
std::string relativePrefixFromObj(const std::string& objPath) {
    const std::size_t slash = objPath.find_last_of("/\\");
    if (slash == std::string::npos) {
        return "";
    }
    std::string prefix;
    for (std::size_t i = 0; i <= slash; ++i) {
        if (objPath[i] == '/' || objPath[i] == '\\') {
            prefix += "../";
        }
    }
    return prefix;
}

// Mean inclination from vertical of first-order branch wood by crown decile,
// alongside the angle each bough emerged at. Geometry alone cannot separate a
// young primary bough from a secondary twig near the stem, so this reads the
// simulation's own branch orders.
void printBranchAngleProfile(const StemGraph& graph, const std::vector<Vec3>& tips) {
    float top = 0.0f;
    float lowestBranch = 0.0f;
    bool anyBranch = false;
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive) {
            continue;
        }
        top = std::max(top, tips[i].z);
        if (node.branchOrder == 0) {
            continue;
        }
        lowestBranch = anyBranch ? std::min(lowestBranch, tips[i].z) : tips[i].z;
        anyBranch = true;
    }
    if (!anyBranch) {
        return;
    }
    const float depth = std::max(0.001f, top - lowestBranch);

    constexpr std::size_t kDeciles = 10;
    std::array<double, kDeciles> woodSum{};
    std::array<double, kDeciles> woodLength{};
    std::array<double, kDeciles> emergenceSum{};
    std::array<std::size_t, kDeciles> emergenceCount{};

    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.branchOrder != 1 || node.parent == kInvalidIndex) {
            continue;
        }
        // Walk to the axis origin on the trunk to bucket the whole axis by
        // where it left the leader, not by where its tip has drooped to.
        Index axisRoot = i;
        while (graph.internode(graph.internode(axisRoot).parent).branchOrder == 1) {
            axisRoot = graph.internode(axisRoot).parent;
        }
        const float originHeight = tips[graph.internode(axisRoot).parent].z;
        const float relative = std::clamp((top - originHeight) / depth, 0.0f, 1.0f);
        const std::size_t decile =
            std::min(kDeciles - 1, static_cast<std::size_t>(relative * kDeciles));
        const double angle = std::acos(std::clamp(node.direction.z, -1.0f, 1.0f)) * 57.29578;
        woodSum[decile] += angle * node.length;
        woodLength[decile] += node.length;
        if (axisRoot == i) {
            emergenceSum[decile] += angle;
            ++emergenceCount[decile];
        }
    }

    std::printf("branch angles (deg from vertical, crown depth 0 = leader tip):\n");
    std::printf("    depth   boughs  emergence   wood\n");
    for (std::size_t decile = 0; decile < kDeciles; ++decile) {
        if (emergenceCount[decile] == 0 && woodLength[decile] <= 0.0) {
            continue;
        }
        std::printf("    %2zu-%3zu%%  %5zu    %6.1f   %6.1f\n", decile * 10, decile * 10 + 10,
                    emergenceCount[decile],
                    emergenceCount[decile] > 0
                        ? emergenceSum[decile] / static_cast<double>(emergenceCount[decile])
                        : 0.0,
                    woodLength[decile] > 0.0 ? woodSum[decile] / woodLength[decile] : 0.0);
    }

    // Secondary branchlets: the pendulous curtain hanging off those boughs,
    // bucketed by the crown depth of the bough that bears it.
    std::array<double, kDeciles> hangSum{};
    std::array<double, kDeciles> hangLength{};
    std::array<std::size_t, kDeciles> hangAxes{};
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.branchOrder < 2 || node.parent == kInvalidIndex) {
            continue;
        }
        Index axisRoot = i;
        while (graph.internode(graph.internode(axisRoot).parent).branchOrder == node.branchOrder) {
            axisRoot = graph.internode(axisRoot).parent;
        }
        const float originHeight = tips[graph.internode(axisRoot).parent].z;
        const float relative = std::clamp((top - originHeight) / depth, 0.0f, 1.0f);
        const std::size_t decile =
            std::min(kDeciles - 1, static_cast<std::size_t>(relative * kDeciles));
        const double angle = std::acos(std::clamp(node.direction.z, -1.0f, 1.0f)) * 57.29578;
        hangSum[decile] += angle * node.length;
        hangLength[decile] += node.length;
        hangAxes[decile] += axisRoot == i ? 1 : 0;
    }

    std::printf("secondary branchlets (order 2+, bucketed by bearing bough):\n");
    std::printf("    depth   axes      hang\n");
    for (std::size_t decile = 0; decile < kDeciles; ++decile) {
        if (hangLength[decile] <= 0.0) {
            continue;
        }
        std::printf("    %2zu-%3zu%%  %5zu    %6.1f\n", decile * 10, decile * 10 + 10,
                    hangAxes[decile], hangSum[decile] / hangLength[decile]);
    }
}

// Variant conditions are authored against TwigCard::foliageDensity, so the
// distribution of that quantity is what a threshold has to be chosen from.
void printFoliageVariantProfile(const std::vector<TwigCard>& cards, const MaterialSet& materials) {
    if (cards.empty()) {
        return;
    }
    std::vector<float> densities;
    densities.reserve(cards.size());
    std::vector<std::size_t> perVariant(std::max<std::size_t>(materials.foliage.size(), 1), 0);
    std::size_t unmatched = 0;
    const std::vector<TwigCardVariant> selectors = foliageSelectors(materials);
    for (const TwigCard& card : cards) {
        densities.push_back(card.foliageDensity);
        perVariant[card.variant] += 1;
        bool matched = false;
        for (const TwigCardVariant& selector : selectors) {
            matched = matched || selector.accepts(card);
        }
        unmatched += matched ? 0 : 1;
    }
    std::sort(densities.begin(), densities.end());
    const auto quantile = [&densities](double fraction) {
        const std::size_t index =
            std::min(densities.size() - 1,
                     static_cast<std::size_t>(fraction * static_cast<double>(densities.size())));
        return static_cast<double>(densities[index]);
    };
    std::printf("foliage density (twig clothing, 1 = every cluster slot filled):\n");
    std::printf("    min %.2f  p25 %.2f  median %.2f  p75 %.2f  max %.2f\n", quantile(0.0),
                quantile(0.25), quantile(0.5), quantile(0.75), quantile(0.999));

    // Split by crown half: the long lower boughs and the short young ones near
    // the apex have different position distributions, and one median over both
    // hides an inner bough that lost its branchlets.
    float cardHeight = 0.0f;
    std::vector<float> upper;
    std::vector<float> lower;
    for (const TwigCard& card : cards) {
        (card.crownDepth < 0.5f ? upper : lower).push_back(card.branchFraction);
        cardHeight = std::max(cardHeight, card.height);
    }
    std::printf("card position along bough (0 = at the trunk, 1 = bough tip):\n");
    for (std::vector<float>* half : {&upper, &lower}) {
        if (half->empty()) {
            continue;
        }
        std::sort(half->begin(), half->end());
        const auto at = [half](double f) {
            return static_cast<double>(
                (*half)[std::min(half->size() - 1,
                                 static_cast<std::size_t>(f * static_cast<double>(half->size())))]);
        };
        std::printf("    %-11s %4zu cards   p25 %.2f  median %.2f  p75 %.2f\n",
                    half == &upper ? "upper crown" : "lower crown", half->size(), at(0.25), at(0.5),
                    at(0.75));
    }
    std::printf("    tallest card %.2f m\n", static_cast<double>(cardHeight));
    if (materials.foliage.size() <= 1) {
        return;
    }
    std::printf("    variant                       cards\n");
    for (std::size_t i = 0; i < materials.foliage.size(); ++i) {
        std::printf("    %-28s %5zu\n", fileNameOf(materials.foliage[i].maps.color).c_str(),
                    perVariant[i]);
    }
    if (unmatched > 0) {
        std::printf("    %zu cards matched no variant and fell back to the first\n", unmatched);
    }
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
    std::printf("    --shade <mode>       self-shading: raycast (default) or off\n");
    std::printf("    --materials <file>   material set JSON; writes an .mtl beside the OBJ\n");
    std::printf("    --foliage <mode>     cards (default: one textured twig card per twig,\n");
    std::printf("                         fine branchlets not meshed) or clusters\n");
    std::printf("    --branch-angles      print primary branch inclination by crown decile\n");
    std::printf("    --lod <0..3>         detail level of the written mesh: 0 near (default),\n");
    std::printf("                         3 a baked cross-quad impostor\n");
    std::printf("    --lod-report         write every level and print its vertex budget\n");
    return 1;
}

enum class FoliageMode { TwigCards, LeafClusters };

int runGrow(std::uint64_t seed, std::uint32_t steps, const SpeciesDefinition& species,
            const char* objPath, const char* skeletonPath, ShadeModel shadeModel,
            FoliageMode foliageMode, const char* materialsPath, const MaterialSet& materials,
            bool branchAngles, std::uint32_t lodLevel, bool lodReport) {
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
    float crownBase = height;
    for (const FoliageCluster& cluster : clusters) {
        crownBase = std::min(crownBase, cluster.position.z);
    }
    std::printf("foliage:     %zu clusters, crown base %.2f m\n", clusters.size(),
                static_cast<double>(clusters.empty() ? 0.0f : crownBase));
    std::printf("trunk:       %.2f m diameter at base\n",
                static_cast<double>(2.0f * plant.graph.internode(0).radius));

    // Bare bole: the highest stub sits just under the cleared band, so the two
    // together say whether clearance ran and where it left the crown base.
    std::size_t stubs = 0;
    float highestStub = 0.0f;
    float thinnestStub = 0.0f;
    std::vector<float> snagHeights; // attachment heights, one entry per snag
    std::vector<float> whorlHeights; // the same, for surviving primary boughs
    for (Index i = 0; i < static_cast<Index>(plant.graph.internodeCount()); ++i) {
        const Internode& node = plant.graph.internode(i);
        const bool primaryBough = node.branchOrder == 1 && node.parent != kInvalidIndex &&
                                  plant.graph.internode(node.parent).branchOrder == 0;
        if (node.alive) {
            if (primaryBough) {
                whorlHeights.push_back(tips[node.parent].z);
            }
            continue;
        }
        if (!node.stub) {
            continue;
        }
        if (primaryBough) {
            snagHeights.push_back(tips[node.parent].z);
        }
        highestStub = std::max(highestStub, tips[i].z);
        thinnestStub = stubs == 0 ? node.radius : std::min(thinnestStub, node.radius);
        ++stubs;
    }
    std::printf("deadwood:    %zu stub internodes, highest %.2f m, thinnest radius %.3f m\n", stubs,
                static_cast<double>(highestStub), static_cast<double>(thinnestStub));

    // Tiers are attachment heights collapsed to whorls, so the two lines read
    // as the ladder of snags below the crown and the tiers of live boughs above.
    const auto printTiers = [](const char* label, std::vector<float>& heights) {
        if (heights.empty()) {
            return;
        }
        std::sort(heights.begin(), heights.end());
        std::printf("%s", label);
        float previous = -1.0f;
        std::size_t tiers = 0;
        for (const float attachment : heights) {
            if (attachment - previous > 0.05f) {
                std::printf("%s %.2f", tiers == 0 ? "" : " |", static_cast<double>(attachment));
                ++tiers;
            }
            previous = attachment;
        }
        std::printf("  (%zu tiers)\n", tiers);
    };
    printTiers("snag tiers: ", snagHeights);
    printTiers("live tiers: ", whorlHeights);
    std::printf("invariants:  %s\n", valid ? "ok" : errors.c_str());

    if (branchAngles) {
        printBranchAngleProfile(plant.graph, tips);
    }

    if (lodReport) {
        // The whole ladder in one place: what each level costs is the only way
        // to tell whether a budget holds, and it is cheap to build them all.
        BranchMeshParameters reportBranches;
        reportBranches.uvLengthScale = materials.barkUvLengthScale;
        reportBranches.uvGirthScale = materials.barkUvLengthScale;
        const TwigCardParameters reportCards = twigCardParameters(materials, finalParameters, seed);
        std::printf("lod budget:  level   vertices  triangles   bark v  foliage v   cards\n");
        for (std::uint32_t level = 0; level < kLodLevels; ++level) {
            LodMesh built;
            buildLodMesh(plant.graph, clusters, tips, level, reportBranches, reportCards, built);
            std::printf("               %u %10zu %10zu %8zu %10zu %7zu\n", level,
                        built.vertexCount(), built.triangleCount(), built.bark.vertexCount(),
                        built.foliage.vertexCount(), built.cards);
        }
    }

    if (objPath != nullptr) {
        BranchMeshParameters meshParameters; // stored radii: simulated by M5 thickening
        meshParameters.uvLengthScale = materials.barkUvLengthScale;
        meshParameters.uvGirthScale = materials.barkUvLengthScale;
        TwigCardParameters cardParameters = twigCardParameters(materials, finalParameters, seed);

        LodMesh built;
        Mesh mesh;
        Mesh leaves;
        std::vector<MeshSection> leafSections;
        std::size_t cardCount = 0;
        if (foliageMode == FoliageMode::TwigCards) {
            buildLodMesh(plant.graph, clusters, tips, lodLevel, meshParameters, cardParameters,
                         built);
            mesh = std::move(built.bark);
            leaves = std::move(built.foliage);
            leafSections = std::move(built.sections);
            cardCount = built.cards;
            if (!lodIsImpostor(lodLevel)) {
                std::vector<TwigCard> profileCards;
                buildLodCards(plant.graph, clusters, tips, lodLevel, cardParameters, profileCards);
                printFoliageVariantProfile(profileCards, materials);
            }
        } else {
            FoliageMeshParameters foliageParameters;
            foliageParameters.seed = seed;
            generateFoliageMesh(clusters, foliageParameters, leaves);
            cardCount = clusters.size();
            // Cluster billboards carry no twig to match a variant against, so
            // they all take the first foliage material.
            leafSections = {{0, 0, static_cast<std::uint32_t>(leaves.indices.size())}};
            generateBranchMesh(plant.graph, meshParameters, mesh);
        }

        std::string meshErrors;
        if (!validateMesh(mesh, &meshErrors) || !validateMesh(leaves, &meshErrors)) {
            std::printf("mesh:        INVALID: %s\n", meshErrors.c_str());
            return 2;
        }

        std::string libraryName;
        if (materialsPath != nullptr) {
            const std::string libraryPath = materialLibraryPath(objPath);
            std::ofstream library(libraryPath);
            if (!library) {
                std::printf("materials:   FAILED to write %s\n", libraryPath.c_str());
                return 2;
            }
            library << materialLibraryText(materials, relativePrefixFromObj(objPath));
            libraryName = fileNameOf(libraryPath);
            std::printf("materials:   %s -> %s\n", materials.name.c_str(), libraryPath.c_str());
        }

        std::vector<std::string> leafGroups;
        for (std::size_t i = 0; i < materials.foliage.size(); ++i) {
            leafGroups.push_back(foliageMaterialName(materials, i));
        }
        if (leafGroups.empty()) {
            leafGroups.push_back("foliage");
        }

        if (writeTreeObj(mesh, leaves, leafSections, leafGroups, objPath, libraryName)) {
            std::printf("mesh:        wrote %s (%zu vertices, %zu triangles, %zu %s)\n", objPath,
                        mesh.vertexCount() + leaves.vertexCount(),
                        mesh.triangleCount() + leaves.triangleCount(), cardCount,
                        foliageMode == FoliageMode::TwigCards ? "twig cards" : "leaf clusters");
            std::printf("             lod %u: bark %zu v / %zu tris, foliage %zu v / %zu tris\n",
                        lodLevel, mesh.vertexCount(), mesh.triangleCount(), leaves.vertexCount(),
                        leaves.triangleCount());
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
        MaterialSet materials;
        const char* objPath = nullptr;
        const char* skeletonPath = nullptr;
        const char* materialsPath = nullptr;
        ShadeModel shadeModel = ShadeModel::Raycast;
        FoliageMode foliageMode = FoliageMode::TwigCards;
        bool branchAngles = false;
        std::uint32_t lodLevel = 0;
        bool lodReport = false;
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
            } else if (std::strcmp(argv[i], "--materials") == 0 && i + 1 < argc) {
                materialsPath = argv[++i];
                std::string error;
                if (!loadMaterialSetFromFile(materialsPath, materials, &error)) {
                    std::printf("invalid material file %s: %s\n", materialsPath, error.c_str());
                    return 1;
                }
            } else if (std::strcmp(argv[i], "--foliage") == 0 && i + 1 < argc) {
                const char* mode = argv[++i];
                if (std::strcmp(mode, "cards") == 0) {
                    foliageMode = FoliageMode::TwigCards;
                } else if (std::strcmp(mode, "clusters") == 0) {
                    foliageMode = FoliageMode::LeafClusters;
                } else {
                    return printUsage();
                }
            } else if (std::strcmp(argv[i], "--shade") == 0 && i + 1 < argc) {
                const char* mode = argv[++i];
                if (std::strcmp(mode, "raycast") == 0) {
                    shadeModel = ShadeModel::Raycast;
                } else if (std::strcmp(mode, "off") == 0) {
                    shadeModel = ShadeModel::Off;
                } else {
                    return printUsage();
                }
            } else if (std::strcmp(argv[i], "--lod") == 0 && i + 1 < argc) {
                const unsigned long level = std::strtoul(argv[++i], nullptr, 10);
                if (level >= kLodLevels) {
                    return printUsage();
                }
                lodLevel = static_cast<std::uint32_t>(level);
            } else if (std::strcmp(argv[i], "--lod-report") == 0) {
                lodReport = true;
            } else if (std::strcmp(argv[i], "--branch-angles") == 0) {
                branchAngles = true;
            } else {
                return printUsage();
            }
        }
        return runGrow(seed, steps, species, objPath, skeletonPath, shadeModel, foliageMode,
                       materialsPath, materials, branchAngles, lodLevel, lodReport);
    }
    return printUsage();
}

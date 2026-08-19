#pragma once

#include <cstdint>
#include <vector>

#include "flora/geometry/mesh.hpp"
#include "flora/structure/foliage.hpp"
#include "flora/structure/stem_graph.hpp"

namespace flora {

// Botanical LOD operator (ARCHITECTURE.md §9): a leafy twig -- its fine
// branchlets plus every foliage cluster on them -- is replaced by one textured
// card. The card texture is a photographed twig, so the geometry that the card
// stands in for is not meshed at all: see BranchMeshParameters::cullBelowRadius.
//
// Cards are derived render data. Simulation still places clusters at full
// density: they remain the shadow casters and energy producers.
struct TwigCard {
    Index twigBase = kInvalidIndex; // first internode of the represented twig
    Vec3 base;                      // attachment point, plant-local (card pivot)
    Vec3 axis = kUpAxis;            // unit direction from base toward the twig tip
    float width = 0.0f;             // meters, across the card
    float height = 0.0f;            // meters, base to top
    float roll = 0.0f;              // rotation of the card about axis, radians
    std::uint32_t clusterCount = 0; // foliage clusters merged into this card
};

struct TwigCardParameters {
    // Twigs whose base radius is at or below this become cards. Matching
    // GrowthParameters::leafMaxBearingRadius makes every leaf-bearing twig a
    // card; larger values merge more aggressively (coarser LOD).
    float maxBearingRadius = 0.02f;
    // Texture proportions: card width = height * aspect, and the twig stem
    // enters the texture at (pivotU, 0). Defaults suit a portrait twig atlas.
    float aspect = 0.85f;
    float pivotU = 0.5f;
    float sizeScale = 1.0f;  // uniform card scale
    float minHeight = 0.12f; // meters; keeps single-cluster twigs visible
    // A card stands for one photographed twig, so a long branchlet run is cut
    // into several cards rather than stretched into one implausible sprite.
    float maxLength = 0.6f; // meters of stem per card
    std::uint32_t quadsPerCard = 2; // crossed quads
    std::uint64_t seed = 0;
};

// Groups clusters onto the twig they grow on and emits one card per twig,
// sized to cover the clusters it absorbs. Deterministic: cards come out in
// twig-base index order and the roll comes from a seeded stream.
//
// Complexity: O(internodes + clusters), two index-order passes, no per-twig
// allocation. tipPositions must come from computeTipPositions on the same graph.
void buildTwigCards(const StemGraph& graph, const std::vector<FoliageCluster>& clusters,
                    const std::vector<Vec3>& tipPositions, const TwigCardParameters& parameters,
                    std::vector<TwigCard>& cards);

// Crossed quads per card, pivoted at the attachment point, UV 0-1 with (0,0)
// at the bottom-left of the texture. Normals are the quad normals; two-sided
// or alpha-tested rendering is the consumer's job. Output mesh is cleared.
void generateTwigCardMesh(const std::vector<TwigCard>& cards,
                          const TwigCardParameters& parameters, Mesh& mesh);

} // namespace flora

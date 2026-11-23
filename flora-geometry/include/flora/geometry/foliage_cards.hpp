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
    // Fraction of the cluster slots the represented twig's stem could carry
    // that are actually filled: clusters times the species' cluster spacing,
    // over the stem length the card covers. 1 is fully clothed; a long shaded
    // branchlet system that kept only a few clusters sits well below it. Values
    // above 1 are possible where cluster spacing carries across a junction.
    float foliageDensity = 0.0f;
    // Relative crown depth of the attachment point: 0 at the topmost foliage,
    // 1 at the crown base. Same convention as the growth set-point gradient.
    float crownDepth = 0.0f;
    // How far out along its bough the card sits: 0 where the bough leaves the
    // trunk, 1 at the bough's farthest tip. Inner and outer twigs of one bough
    // are different organs -- the inner ones are the shaded, self-pruning end
    // of it -- and crown depth cannot tell them apart, since a whole bough
    // shares one crown depth. Cards borne on the leader itself sit at 1.
    float branchFraction = 1.0f;
    std::uint32_t variant = 0; // index into TwigCardParameters::variants
};

// Picks the photographed twig that suits a card. Every bound is optional and
// all of them must hold; a card matching no variant falls back to index 0.
struct TwigCardVariant {
    float minFoliageDensity = 0.0f;
    float maxFoliageDensity = 1.0e30f;
    std::uint32_t minClusters = 0;
    std::uint32_t maxClusters = 0xFFFFFFFFu;
    float minCrownDepth = 0.0f;
    float maxCrownDepth = 1.0f;
    float minBranchFraction = 0.0f;
    float maxBranchFraction = 1.0f;
    // Relative odds when several variants accept the same card. Overlapping
    // bands are what keep a density series from banding visibly.
    float weight = 1.0f;

    bool accepts(const TwigCard& card) const;
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
    // Sibling twigs one card stands in for. The LOD merges a whole branchlet
    // run into a single photographed twig, but a bough carries a fan of them
    // side by side. Emitting that fan here is how the curtain gets its density:
    // the alternative is driving the simulation hard enough to grow the extra
    // branchlets, and the same LOD would immediately merge them away again, so
    // the wood would cost energy and shade without ever reaching the screen.
    // 1 emits the card alone.
    std::uint32_t instancesPerCard = 1;
    float instanceSpread = 0.5f;       // lateral offset, as a fraction of card width
    float instanceScaleJitter = 0.25f; // size variation between siblings, +/- fraction
    // Meters of twig one photograph shows. A plate bigger than this stands for
    // more twig than the picture holds, so its UVs tile rather than stretch. A
    // coarse level aggregates a whole bough onto one plate, and stretching a
    // single twig across it makes the needles grow as the viewer walks away --
    // the opposite of what a detail level is for. Repeats are whole numbers, so
    // a tiled twig is never cut mid-needle at the seam. 0 maps the texture once
    // however large the plate is, which is the near-level reading: there, one
    // card really is one twig.
    float textureTwigLength = 0.0f;
    std::uint64_t seed = 0;
    // Normalizer for TwigCard::foliageDensity: the species'
    // GrowthParameters::leafClusterSpacing, which is the closest two clusters
    // are placed, so a fully clothed shoot lands near a density of 1.
    float clusterSpacing = 0.3f;
    // Photographed twigs to choose between, in material order. Empty means one
    // material for every card.
    std::vector<TwigCardVariant> variants;
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
// at the bottom-left of the texture -- the twig photograph's stem end, which is
// what the pivot attaches to the tree. Normals are the quad normals; two-sided
// or alpha-tested rendering is the consumer's job. Output mesh is cleared.
//
// Cards are emitted grouped by variant, so each variant owns one contiguous
// index run; sections receives those runs in variant order when given, and a
// single entry when the parameters carry no variants.
void generateTwigCardMesh(const std::vector<TwigCard>& cards,
                          const TwigCardParameters& parameters, Mesh& mesh,
                          std::vector<MeshSection>* sections = nullptr);

} // namespace flora

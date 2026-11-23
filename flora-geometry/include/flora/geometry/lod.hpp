#pragma once

#include <cstdint>
#include <vector>

#include "flora/geometry/branch_mesh.hpp"
#include "flora/geometry/foliage_cards.hpp"
#include "flora/geometry/mesh.hpp"
#include "flora/structure/foliage.hpp"
#include "flora/structure/stem_graph.hpp"

namespace flora {

// A level-3 impostor is one camera-facing quad carrying a baked atlas of the
// near tree seen from kImpostorAngles yaws. Crossed geometry planes are what a
// renderer builds when it has no baked texture; once the crown is a photograph
// of itself, extra planes only add self-intersection artefacts.
inline constexpr std::uint32_t kImpostorVertices = 4;

// Yaws the impostor atlas is baked at, and the grid they are packed in. Eight
// at 45 degrees is the point where a conifer's outline stops visibly rotating
// between samples: the crown is near enough radially symmetric that the error
// inside a 45 degree arc is smaller than the alpha fringe.
inline constexpr std::uint32_t kImpostorAngles = 8;
inline constexpr std::uint32_t kImpostorAtlasColumns = 4;
inline constexpr std::uint32_t kImpostorAtlasRows = 2;

// The crown outline of the near mesh, measured band by band rather than fitted
// to an assumed cone. Two things need it and both need the same answer: the
// impostor, which is that outline, and the coarse levels, whose grown cards must
// not push past the outline the near level established.
struct CrownEnvelope {
    static constexpr std::uint32_t kBands = 48;

    Vec3 centre{};             // trunk axis in plan, from the card centroid
    float top = 0.0f;          // highest foliage, meters above the ground
    float radius = 0.0f;       // widest band
    float bands[kBands] = {};  // farthest card reach in each height band

    bool valid() const { return top > 0.0f && radius > 0.0f; }
    // Farthest the crown reaches from the trunk axis at this height. Heights
    // outside the crown clamp to the nearest band.
    float radiusAt(float height) const;
};

// Measures the envelope of a card set. Pass the level-0 set: the outline a
// coarse level has to respect is the one the viewer last saw.
CrownEnvelope measureCrownEnvelope(const std::vector<TwigCard>& cards);

// Discrete detail levels for one plant (ARCHITECTURE.md §9).
//
// A coarse level is a *thinning of level 0*, never a second derivation of the
// tree. Re-deriving -- telling level 2 that a whole bough counts as one twig,
// so it grows its own bigger plates in its own places -- gives a mesh that is
// cheap and is not the same tree: the silhouette shifts on the transition, and
// the crown pops. So the card set is built once, at level 0, and a level drops
// cards out of it and enlarges the survivors to cover the area the dropped ones
// held. Every card a coarse level draws is a card level 0 drew, in the place
// level 0 drew it. What varies with distance is how many of them, how many
// crossed quads each gets, and how coarse the bole's rings are.
//
// Level 3 leaves the structure behind, because below a certain size a tree is a
// silhouette rather than an arrangement of parts.
//
// A level is a rendering decision, never simulation state: the structure and
// the foliage clusters are identical at every level.
inline constexpr std::uint32_t kLodLevels = 4;

// Rewrites a level-0 configuration for the requested level. The incoming
// parameters carry the species and material authority (what a twig is, how
// large a card sits, how coarse the bole may get); a level only ever reduces
// them, so raising the level can never add geometry. Returns false and leaves
// both untouched when level is not a valid level.
//
// What a twig is does not vary by level any more -- that is what keeps the card
// set common to the whole ladder -- so the card sizing fields come back
// untouched and only the per-level counts move.
bool applyLodProfile(std::uint32_t level, BranchMeshParameters& branches,
                     TwigCardParameters& cards);

// Share of the level-0 cards a level keeps. 1 at the near levels, which thin by
// dropping the sibling fan rather than by dropping cards.
float lodCardKeepFraction(std::uint32_t level);

// Drops cards to keepFraction of the input, interior first.
//
// Which cards go matters more than how many. A conifer's shape is carried by
// its outer surfaces -- the branchlet tips that cut the outline, the drooping
// skirt at the crown base, the leader's own foliage -- while the cards packed
// around the bole are occluded by those surfaces from every direction that
// matters. So the budget is filled in that order and the interior pays for it:
// the spire is never touched, then outer tips, then the skirt, and only what is
// left over comes out of the shaded core.
//
// Survivors grow to cover some of what was dropped, but the growth is capped
// hard (kMaxCardGrowth). An inflated plate spans the gap between two whorls and
// welds the shelves into a wall, which costs the tiering that the thinning order
// exists to protect; a slightly thinner crown is the cheaper loss. Growth is
// then clamped again against the envelope, so no survivor reaches past the
// outline the near level drew.
//
// Selection within a class is a fixed stride over input order, which is
// internode index order and so spatially coherent: a stride removes every nth
// twig along a bough instead of hollowing whichever region a random draw hit.
void thinTwigCards(const std::vector<TwigCard>& cards, float keepFraction,
                   const CrownEnvelope& envelope, std::vector<TwigCard>& out);

// The cards a level actually draws: the level-0 set, thinned for that level.
// Callers that need the card list itself (a variant report, a debug overlay)
// should use this rather than buildTwigCards, or they describe a level nobody
// draws. Returns false and empties out when level is not a valid level.
bool buildLodCards(const StemGraph& graph, const std::vector<FoliageCluster>& clusters,
                   const std::vector<Vec3>& tipPositions, std::uint32_t level,
                   TwigCardParameters cards, std::vector<TwigCard>& out);

// True when the level replaces the tree with a baked impostor and neither the
// branch mesh nor the card mesh should be generated.
bool lodIsImpostor(std::uint32_t level);

// Everything one level needs in order to draw: wood, foliage, and the material
// runs of the foliage mesh.
struct LodMesh {
    Mesh bark;
    Mesh foliage;
    std::vector<MeshSection> sections;
    std::size_t cards = 0;

    std::size_t vertexCount() const { return bark.vertexCount() + foliage.vertexCount(); }
    std::size_t triangleCount() const { return bark.triangleCount() + foliage.triangleCount(); }
};

// Builds one level from the structure and its foliage. The parameters passed in
// are the level-0 configuration (species authority for what a twig is, material
// authority for how a plate is cut); applyLodProfile reduces them internally,
// so a caller only ever authors level 0. At the impostor level the wood mesh
// comes out empty. Deterministic for a given structure and configuration.
void buildLodMesh(const StemGraph& graph, const std::vector<FoliageCluster>& clusters,
                  const std::vector<Vec3>& tipPositions, std::uint32_t level,
                  BranchMeshParameters branches, TwigCardParameters cards, LodMesh& out);

// Level 3: one quad spanning the crown envelope, on material 0 (an impostor is
// one baked texture, not a variant atlas). Pivoted on the ground under the crown
// centre so it plants like the tree it stands for, and emitted in the XZ plane
// facing -Y; the renderer spins it about Z to face the camera and picks the
// atlas tile from the yaw it turned through. UVs are the full 0-1 of one tile,
// so tile selection is the renderer's offset to apply, not a bake-time
// commitment in the mesh.
//
// The silhouette is no longer geometry, because it is in the alpha of the baked
// atlas -- which is the only way a distant tree keeps the outline it had up
// close, cut planes included. Output mesh is cleared; sections receives the
// single run when given. Emits nothing for an invalid envelope.
void generateImpostorMesh(const CrownEnvelope& envelope, Mesh& mesh,
                          std::vector<MeshSection>* sections = nullptr);

// Atlas tile for a viewing yaw, in radians about +Z measured from +X: the
// direction the camera looks *from*. Returns the column/row of the 4x2 grid and
// the UV scale-and-offset that selects it.
struct ImpostorTile {
    std::uint32_t index = 0; // 0 .. kImpostorAngles - 1
    float uScale = 1.0f / static_cast<float>(kImpostorAtlasColumns);
    float vScale = 1.0f / static_cast<float>(kImpostorAtlasRows);
    float uOffset = 0.0f;
    float vOffset = 0.0f;
};

// The yaw the given atlas tile was baked at. The bake and the lookup have to
// agree on this or the tree faces the wrong way, so they share one function.
float impostorAngleYaw(std::uint32_t index);

// Nearest baked yaw to the one asked for.
ImpostorTile impostorTileForYaw(float yaw);

} // namespace flora

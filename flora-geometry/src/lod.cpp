#include "flora/geometry/lod.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "flora/math/rotation.hpp"

namespace flora {

namespace {

constexpr float kPi = 3.1415927f;

// What the card operator treats as a twig, against the species' own
// leaf-bearing radius. Pulled in below it so primary bough wood is swept as
// tubes rather than swallowed into plates: a bough has to read as a limb, and a
// plate standing on a limb is not one. It applies to the whole ladder and not
// to a level, because the ladder shares one card set -- a level that moved this
// would be deriving its own cards, which is exactly the re-derivation that made
// the crown jump on the transition.
constexpr float kCardBearingScale = 0.70f;

// Relative crown depth within which a card is exempt from thinning: the top
// eighth of the crown, which for an excurrent conifer is the leader and the
// season's whorls, and is where the card count per unit of silhouette is
// already lowest.
constexpr float kApexKeepDepth = 0.12f;

// Ceiling on the compensating growth a survivor gets. Area compensation wants
// 1/sqrt(keep); the cap is what stops a plate spanning the gap between two
// whorls and welding the shelves into a wall. Set just under the point where
// that starts to show, so the surviving plates close the air between them
// without the tiering going.
constexpr float kMaxCardGrowth = 1.30f;

// Class boundaries for interior-biased culling. Outward along the bough and low
// in the crown are both surfaces; what is left is the occluded core.
constexpr float kOuterBranchFraction = 0.6f;
constexpr float kSkirtCrownDepth = 0.75f;

// How hard each class is culled relative to the others. Bias, not priority: the
// earlier scheme filled the budget class by class, which meant the class the
// budget ran out in got nothing at all -- whole whorl sections vanished and left
// the bole bare with the outer tips floating off it. Every class is sampled
// here, the core just more sparsely, so thinning reads as a crown losing density
// rather than a crown losing parts.
constexpr float kClassBias[] = {1.0f, 1.30f, 1.15f, 0.70f};

// What each level does to the level-0 configuration. Card *sizing* is absent on
// purpose: every level draws level-0 cards, so only the counts move.
struct LodStep {
    std::uint32_t radialSegments; // absolute: the bole's ring count is the budget
    std::uint32_t minimumRadialSegments;
    std::uint32_t branchRadialSegments;
    std::uint32_t minimumBranchRadialSegments;
    std::uint32_t instancesPerCard;
    std::uint32_t quadsPerCard;
    float cardKeepFraction; // share of the level-0 cards drawn
};

constexpr LodStep kSteps[kLodLevels] = {
    // 0 - near. Rounded bole, boughs swept as tubes, and the curtain is a fan
    // of three crossed plates per twig. Every card is drawn.
    {10, 4, 5, 3, 3, 2, 1.00f},
    // 1 - mid. Coarser bole, and one plate per twig instead of a fan: the
    // parallax inside a fan is under a pixel here, and the fan is 3x the
    // foliage. The cards themselves are untouched, so the silhouette does not
    // move on the transition -- the crown loses its interior, not its outline.
    {6, 4, 4, 3, 1, 2, 1.00f},
    // 2 - far. The interior is already gone, so the only foliage left to spend
    // is the cards themselves: about half of them, each grown to close the gap
    // its neighbour left. The bole is a prism.
    //
    // Half and not a quarter, because this level sits between a full crown and
    // a baked photograph of one and has to be readable as the same tree as
    // both. At a quarter it was neither: a bare pole with the outer tips
    // floating off it, and the pop in either direction was worse than the cost
    // of the cards it saved.
    //
    // The cull radius does not widen here. Letting the cards swallow bough-scale
    // wood is what made this level a different tree: the boughs disappeared into
    // plates sitting where no level-0 plate sat, so the crown jumped on the
    // transition and the apex went blunt where a clamped plate overshot the
    // leader. Coarse rings on the real boughs cost a few hundred vertices and
    // keep the branch structure the near levels showed.
    {4, 3, 3, 3, 1, 2, 0.52f},
    // 3 - impostor. Only the card set matters, and only to measure the crown
    // outline; segment and quad counts are unused.
    {4, 3, 3, 3, 1, 1, 0.25f},
};

float planarDistance(const Vec3& from, const Vec3& to) {
    const float dx = from.x - to.x;
    const float dy = from.y - to.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Height band a card occupies and its farthest horizontal reach from the trunk
// axis. A card is a plate standing on its base and running along its axis, so
// both ends bound it.
void cardExtent(const TwigCard& card, const Vec3& centre, float& low, float& high, float& reach) {
    const Vec3 base = card.base;
    const Vec3 tip = base + card.axis.normalizedOr(kUpAxis) * card.height;
    low = std::min(base.z, tip.z);
    high = std::max(base.z, tip.z);
    reach = std::max(planarDistance(base, centre), planarDistance(tip, centre)) + card.width * 0.5f;
}

// Where a card sits in the cull order. Lower goes last.
std::uint32_t cardClass(const TwigCard& card) {
    if (card.crownDepth <= kApexKeepDepth) {
        return 0; // the leader's own foliage: the spire is this and nothing else
    }
    if (card.branchFraction >= kOuterBranchFraction) {
        return 1; // branchlet tips, which are the cards that cut the outline
    }
    if (card.crownDepth >= kSkirtCrownDepth) {
        return 2; // the drooping lower skirt, read against the sky from below
    }
    return 3; // shaded core, hidden behind all of the above
}

constexpr std::uint32_t kCardClasses = 4;

} // namespace

bool lodIsImpostor(std::uint32_t level) { return level == kLodLevels - 1; }

bool applyLodProfile(std::uint32_t level, BranchMeshParameters& branches,
                     TwigCardParameters& cards) {
    if (level >= kLodLevels) {
        return false;
    }
    const LodStep& step = kSteps[level];

    branches.radialSegments = step.radialSegments;
    branches.minimumRadialSegments = step.minimumRadialSegments;
    branches.branchRadialSegments = step.branchRadialSegments;
    branches.minimumBranchRadialSegments = step.minimumBranchRadialSegments;

    // The photograph was shot as a level-0 card, so that is the twig it shows.
    // Fixing the unit here is what makes needle size a property of the tree
    // rather than of the level looking at it: a survivor grown to cover its
    // dropped neighbours tiles the picture instead of magnifying it.
    cards.textureTwigLength = cards.maxLength * cards.sizeScale;

    // Level-independent: the whole ladder shares one card set.
    cards.maxBearingRadius *= kCardBearingScale;

    cards.instancesPerCard = step.instancesPerCard;
    cards.quadsPerCard = step.quadsPerCard;

    // The card threshold and the mesh cull are two halves of one boundary: wood
    // a card's photograph already contains must not also be swept. Coupling
    // them here is what keeps a level from double-representing its own twigs.
    branches.cullBelowRadius = cards.maxBearingRadius;
    return true;
}

float lodCardKeepFraction(std::uint32_t level) {
    return level < kLodLevels ? kSteps[level].cardKeepFraction : 1.0f;
}

float CrownEnvelope::radiusAt(float height) const {
    if (!(top > 0.0f)) {
        return 0.0f;
    }
    const float band = std::floor(height * static_cast<float>(kBands) / top);
    const auto index = static_cast<std::size_t>(
        std::clamp(band, 0.0f, static_cast<float>(kBands - 1)));
    return bands[index];
}

CrownEnvelope measureCrownEnvelope(const std::vector<TwigCard>& cards) {
    CrownEnvelope envelope;
    if (cards.empty()) {
        return envelope;
    }
    for (const TwigCard& card : cards) {
        envelope.top = std::max(envelope.top, card.base.z + card.height);
        envelope.centre = envelope.centre + card.base;
    }
    envelope.centre = envelope.centre * (1.0f / static_cast<float>(cards.size()));
    if (!(envelope.top > 0.0f)) {
        return envelope;
    }

    const float scale = static_cast<float>(CrownEnvelope::kBands) / envelope.top;
    for (const TwigCard& card : cards) {
        float low = 0.0f;
        float high = 0.0f;
        float reach = 0.0f;
        cardExtent(card, envelope.centre, low, high, reach);
        envelope.radius = std::max(envelope.radius, reach);
        const auto first = static_cast<std::int32_t>(std::floor(low * scale));
        const auto last = static_cast<std::int32_t>(std::floor(high * scale));
        const std::int32_t from = std::max(0, first);
        const std::int32_t to =
            std::min(last, static_cast<std::int32_t>(CrownEnvelope::kBands) - 1);
        for (std::int32_t band = from; band <= to; ++band) {
            float& held = envelope.bands[static_cast<std::size_t>(band)];
            held = std::max(held, reach);
        }
    }
    return envelope;
}

void thinTwigCards(const std::vector<TwigCard>& cards, float keepFraction,
                   const CrownEnvelope& envelope, std::vector<TwigCard>& out) {
    out.clear();
    const float keep = std::clamp(keepFraction, 0.0f, 1.0f);
    if (keep >= 1.0f) {
        out = cards;
        return;
    }
    if (cards.empty() || keep <= 0.0f) {
        return;
    }

    // Per-class keep shares: the requested fraction, tilted by kClassBias, then
    // scaled so the shares actually add up to the fraction that was asked for.
    // Every class keeps a share of its own, so every bough is sampled along its
    // whole length instead of surviving whole in one place and vanishing in
    // another. Solved by bisection because a share saturating at 1 makes the
    // total a piecewise-linear function of the scale rather than a proportional
    // one; sixteen halvings settle it well past float resolution, and a fixed
    // count keeps it deterministic.
    std::size_t population[kCardClasses] = {};
    for (const TwigCard& card : cards) {
        ++population[cardClass(card)];
    }
    // The spire is outside the budget entirely: it is a handful of cards, and
    // thinning it is what turns the leader into a wisp.
    const float wanted = static_cast<float>(cards.size()) * keep;
    const auto totalFor = [&](float scale) {
        float total = static_cast<float>(population[0]);
        for (std::uint32_t rank = 1; rank < kCardClasses; ++rank) {
            total += static_cast<float>(population[rank]) *
                     std::min(1.0f, keep * kClassBias[rank] * scale);
        }
        return total;
    };
    float low = 0.0f;
    float high = 1.0f / std::max(keep * kClassBias[kCardClasses - 1], 1.0e-4f);
    for (std::uint32_t step = 0; step < 16; ++step) {
        const float middle = (low + high) * 0.5f;
        (totalFor(middle) < wanted ? low : high) = middle;
    }
    const float scale = (low + high) * 0.5f;

    float share[kCardClasses] = {1.0f, 0.0f, 0.0f, 0.0f};
    for (std::uint32_t rank = 1; rank < kCardClasses; ++rank) {
        share[rank] = std::min(1.0f, keep * kClassBias[rank] * scale);
    }

    // Compensating growth, capped: see kMaxCardGrowth. The apex is exempt from
    // both the cull and the growth -- it was never thinned, so there is nothing
    // there to compensate for, and growing it is what blunted the spire.
    const float growth = std::min(1.0f / std::sqrt(keep), kMaxCardGrowth);

    // One stride per class, each starting a stride short of full so the first
    // card of every class is always taken.
    float credit[kCardClasses];
    for (std::uint32_t rank = 0; rank < kCardClasses; ++rank) {
        credit[rank] = 1.0f - share[rank];
    }
    out.reserve(static_cast<std::size_t>(static_cast<float>(cards.size()) * keep) + 1);
    for (const TwigCard& card : cards) {
        const std::uint32_t rank = cardClass(card);
        if (share[rank] <= 0.0f) {
            continue;
        }
        credit[rank] += share[rank];
        if (credit[rank] < 1.0f) {
            continue;
        }
        credit[rank] -= 1.0f;
        if (rank == 0) {
            out.push_back(card); // spire, at its authored size
            continue;
        }

        TwigCard survivor = card;
        survivor.height *= growth;
        survivor.width *= growth;

        // The envelope has the last word. A grown plate that reaches past the
        // outline the near level drew makes the crown swell on the transition,
        // and swelling is worse than thinning because it moves the silhouette.
        if (envelope.valid()) {
            const Vec3 axis = survivor.axis.normalizedOr(kUpAxis);
            const Vec3 tip = survivor.base + axis * survivor.height;
            const float limit = envelope.radiusAt(tip.z);
            const float reach = planarDistance(tip, envelope.centre) + survivor.width * 0.5f;
            if (reach > limit && reach > 0.0f) {
                const float fit = std::clamp(limit / reach, 1.0f / growth, 1.0f);
                survivor.height *= fit;
                survivor.width *= fit;
            }
            const float over = survivor.base.z + axis.z * survivor.height - envelope.top;
            if (over > 0.0f && axis.z > 0.0f) {
                survivor.height = std::max(card.height, survivor.height - over / axis.z);
            }
        }
        out.push_back(survivor);
    }
}

bool buildLodCards(const StemGraph& graph, const std::vector<FoliageCluster>& clusters,
                   const std::vector<Vec3>& tipPositions, std::uint32_t level,
                   TwigCardParameters cards, std::vector<TwigCard>& out) {
    out.clear();
    BranchMeshParameters unused;
    if (!applyLodProfile(level, unused, cards)) {
        return false;
    }
    // Card sizing does not vary by level, so this is the level-0 set whatever
    // level was asked for -- which is what makes its envelope the near one.
    std::vector<TwigCard> full;
    buildTwigCards(graph, clusters, tipPositions, cards, full);
    thinTwigCards(full, lodCardKeepFraction(level), measureCrownEnvelope(full), out);
    return true;
}

void buildLodMesh(const StemGraph& graph, const std::vector<FoliageCluster>& clusters,
                  const std::vector<Vec3>& tipPositions, std::uint32_t level,
                  BranchMeshParameters branches, TwigCardParameters cards, LodMesh& out) {
    out.bark.clear();
    out.foliage.clear();
    out.sections.clear();
    out.cards = 0;
    const TwigCardParameters authored = cards;
    if (!applyLodProfile(level, branches, cards)) {
        return;
    }

    if (lodIsImpostor(level)) {
        // Measured against the near crown, not against a thinned one: the
        // outline the impostor has to match is the outline the viewer last saw.
        std::vector<TwigCard> nearCards;
        buildLodCards(graph, clusters, tipPositions, 0, authored, nearCards);
        out.cards = nearCards.size();
        generateImpostorMesh(measureCrownEnvelope(nearCards), out.foliage, &out.sections);
        return;
    }

    std::vector<TwigCard> twigCards;
    buildLodCards(graph, clusters, tipPositions, level, authored, twigCards);
    out.cards = twigCards.size();
    generateTwigCardMesh(twigCards, cards, out.foliage, &out.sections);
    generateBranchMesh(graph, branches, out.bark);
}

void generateImpostorMesh(const CrownEnvelope& envelope, Mesh& mesh,
                          std::vector<MeshSection>* sections) {
    mesh.clear();
    if (sections != nullptr) {
        sections->clear();
    }
    if (!envelope.valid()) {
        return;
    }

    // Spans the crown from the ground up: an impostor is planted, and a crown
    // floating at its own base reads as a hovering bush at any distance. The
    // width is the widest band, because the atlas was framed to that and the
    // alpha carries the taper -- narrowing the quad would clip the boughs the
    // bake already drew.
    const Vec3 centre = envelope.centre;
    const float half = envelope.radius;
    const Vec3 side{half, 0.0f, 0.0f};
    const Vec3 normal{0.0f, -1.0f, 0.0f};

    const Vec3 foot{centre.x, centre.y, 0.0f};
    const Vec3 head{centre.x, centre.y, envelope.top};
    mesh.positions.push_back(foot - side);
    mesh.positions.push_back(foot + side);
    mesh.positions.push_back(head + side);
    mesh.positions.push_back(head - side);
    for (std::uint32_t corner = 0; corner < kImpostorVertices; ++corner) {
        mesh.normals.push_back(normal);
    }
    // One tile's worth. The renderer scales and offsets these into the atlas
    // cell for the yaw it is looking from; baking the cell in here would fix the
    // impostor to one bearing.
    mesh.uvs.push_back({0.0f, 0.0f});
    mesh.uvs.push_back({1.0f, 0.0f});
    mesh.uvs.push_back({1.0f, 1.0f});
    mesh.uvs.push_back({0.0f, 1.0f});

    mesh.indices.push_back(0);
    mesh.indices.push_back(1);
    mesh.indices.push_back(2);
    mesh.indices.push_back(0);
    mesh.indices.push_back(2);
    mesh.indices.push_back(3);
    if (sections != nullptr) {
        sections->push_back({0, 0, static_cast<std::uint32_t>(mesh.indices.size())});
    }
}

float impostorAngleYaw(std::uint32_t index) {
    return 2.0f * kPi * static_cast<float>(index % kImpostorAngles) /
           static_cast<float>(kImpostorAngles);
}

ImpostorTile impostorTileForYaw(float yaw) {
    const float turn = 2.0f * kPi;
    const float step = turn / static_cast<float>(kImpostorAngles);
    // Rounds to the nearest baked bearing rather than the one below, so the
    // error never exceeds half a step in either direction.
    float wrapped = std::fmod(yaw, turn);
    if (wrapped < 0.0f) {
        wrapped += turn;
    }
    const auto index = static_cast<std::uint32_t>(std::floor(wrapped / step + 0.5f)) %
                       kImpostorAngles;

    ImpostorTile tile;
    tile.index = index;
    const std::uint32_t column = index % kImpostorAtlasColumns;
    const std::uint32_t row = index / kImpostorAtlasColumns;
    tile.uOffset = static_cast<float>(column) * tile.uScale;
    // Row 0 of the atlas is the top row of the image, and V runs up.
    tile.vOffset = static_cast<float>(kImpostorAtlasRows - 1 - row) * tile.vScale;
    return tile;
}

} // namespace flora

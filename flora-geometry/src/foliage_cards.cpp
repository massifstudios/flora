#include "flora/geometry/foliage_cards.hpp"

#include <algorithm>
#include <cmath>

#include "flora/math/rotation.hpp"
#include "flora/rng/random.hpp"

namespace flora {

namespace {

constexpr float kPi = 3.1415927f;

// Weighted pick over the variants that accept this card, from the card's own
// stream so the choice is stable under any other card changing. Falls back to
// index 0 when nothing accepts it, which is why the first entry is documented
// as the default.
std::uint32_t selectVariant(const std::vector<TwigCardVariant>& variants, const TwigCard& card,
                            RandomStream& stream) {
    float total = 0.0f;
    for (const TwigCardVariant& variant : variants) {
        total += variant.accepts(card) ? std::max(variant.weight, 0.0f) : 0.0f;
    }
    if (total <= 0.0f) {
        return 0;
    }
    float pick = stream.uniformFloat(0.0f, 1.0f) * total;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        if (!variants[i].accepts(card)) {
            continue;
        }
        pick -= std::max(variants[i].weight, 0.0f);
        if (pick <= 0.0f) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return static_cast<std::uint32_t>(variants.size() - 1);
}

// Where each internode sits along the bough carrying it: the path distance from
// the bough's departure off the trunk, over the bough's whole live reach.
// Three index-order passes, O(internodes).
struct BoughPositions {
    std::vector<float> pathFromRoot;
    std::vector<float> boughOrigin; // path at the bough's departure point
    std::vector<float> reach;       // farthest live tip path in the subtree
    std::vector<Index> boughRoot;

    float fractionAt(const StemGraph& graph, Index node) const {
        const Index bough = boughRoot[node];
        if (bough == kInvalidIndex) {
            return 1.0f; // borne on the leader: no bough to sit part way along
        }
        const float span = reach[bough] - boughOrigin[node];
        if (span <= 0.0f) {
            return 0.0f;
        }
        const float proximal = pathFromRoot[node] - graph.internode(node).length;
        return std::clamp((proximal - boughOrigin[node]) / span, 0.0f, 1.0f);
    }
};

BoughPositions computeBoughPositions(const StemGraph& graph) {
    const std::size_t count = graph.internodeCount();
    BoughPositions result;
    result.pathFromRoot.resize(count, 0.0f);
    result.boughOrigin.resize(count, 0.0f);
    result.boughRoot.resize(count, kInvalidIndex);
    for (Index i = 0; i < static_cast<Index>(count); ++i) {
        const Internode& node = graph.internode(i);
        const float parentPath =
            node.parent == kInvalidIndex ? 0.0f : result.pathFromRoot[node.parent];
        result.pathFromRoot[i] = parentPath + node.length;
        if (node.parent == kInvalidIndex || node.branchOrder == 0) {
            continue; // trunk: boughRoot stays unset
        }
        if (graph.internode(node.parent).branchOrder == 0) {
            result.boughRoot[i] = i; // this internode leaves the trunk
            result.boughOrigin[i] = parentPath;
            continue;
        }
        result.boughRoot[i] = result.boughRoot[node.parent];
        result.boughOrigin[i] = result.boughOrigin[node.parent];
    }
    result.reach = result.pathFromRoot;
    for (Index i = static_cast<Index>(count); i-- > 0;) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.parent == kInvalidIndex) {
            continue;
        }
        result.reach[node.parent] = std::max(result.reach[node.parent], result.reach[i]);
    }
    return result;
}

void emitCardsFor(const std::vector<TwigCard>& cards, const TwigCardParameters& parameters,
                  std::uint32_t material, std::uint32_t materialCount, std::uint32_t quadCount,
                  Mesh& mesh) {
    for (const TwigCard& card : cards) {
        if (card.height <= 0.0f || card.width <= 0.0f) {
            continue;
        }
        if (materialCount > 1 && card.variant != material) {
            continue;
        }
        const Vec3 axis = card.axis.normalizedOr(kUpAxis);
        const std::uint32_t instances = std::max(1u, parameters.instancesPerCard);
        // Measured on the unjittered card so every sibling in a fan shares one
        // mapping: the fan is one twig seen several times, not several sizes of
        // twig. Equal in both axes because the plate is cut to the texture's
        // aspect, so a single count keeps the picture square to itself.
        // Rounded up, never down: a tile short of a whole one magnifies the
        // needles, which is the artefact this exists to remove, and an extra
        // tile only ever makes the twig read finer than the near level's.
        const float repeats =
            parameters.textureTwigLength > 0.0f
                ? std::max(1.0f,
                           std::ceil(card.height / parameters.textureTwigLength - 1.0e-4f))
                : 1.0f;

        for (std::uint32_t instance = 0; instance < instances; ++instance) {
            // The first instance is the card itself, unmoved, so raising the
            // count only ever adds siblings around what a single card drew.
            Vec3 base = card.base;
            float height = card.height;
            float width = card.width;
            float baseRoll = card.roll;
            if (instance > 0) {
                // A stream of its own: the card's roll and variant were drawn
                // in buildTwigCards, and re-deriving here rather than carrying
                // a running generator keeps every material pass identical.
                auto stream = RandomStream::derive(parameters.seed, RngPurpose::Foliage,
                                                   static_cast<std::uint64_t>(card.twigBase),
                                                   static_cast<std::uint64_t>(instance));
                const float jitter = parameters.instanceScaleJitter;
                const float scale = std::max(0.1f, 1.0f + stream.uniformFloat(-jitter, jitter));
                height *= scale;
                width *= scale;
                baseRoll += stream.uniformFloat(0.0f, 2.0f * kPi);
                const Vec3 side = rotateAroundAxis(perpendicularTo(axis), axis, baseRoll);
                // Siblings sit beside and further out along the same twig, so
                // the fan stays inside the reach the represented branchlet had.
                base = base + side * (stream.uniformFloat(-1.0f, 1.0f) * parameters.instanceSpread *
                                      card.width) +
                       axis * (stream.uniformFloat(0.0f, parameters.instanceSpread) * card.height);
            }
            const Vec3 top = axis * height;
            const float leftEdge = -parameters.pivotU * width;
            const float rightEdge = leftEdge + width;

            for (std::uint32_t quad = 0; quad < quadCount; ++quad) {
                const float roll =
                    baseRoll + kPi * static_cast<float>(quad) / static_cast<float>(quadCount);
                const Vec3 side = rotateAroundAxis(perpendicularTo(axis), axis, roll);
                const Vec3 normal = side.cross(axis).normalizedOr(kUpAxis);

                const std::uint32_t vertexBase = static_cast<std::uint32_t>(mesh.positions.size());
                mesh.positions.push_back(base + side * leftEdge);
                mesh.positions.push_back(base + side * rightEdge);
                mesh.positions.push_back(base + side * rightEdge + top);
                mesh.positions.push_back(base + side * leftEdge + top);
                for (int corner = 0; corner < 4; ++corner) {
                    mesh.normals.push_back(normal);
                }
                mesh.uvs.push_back({0.0f, 0.0f});
                mesh.uvs.push_back({repeats, 0.0f});
                mesh.uvs.push_back({repeats, repeats});
                mesh.uvs.push_back({0.0f, repeats});

                mesh.indices.push_back(vertexBase);
                mesh.indices.push_back(vertexBase + 1);
                mesh.indices.push_back(vertexBase + 2);
                mesh.indices.push_back(vertexBase);
                mesh.indices.push_back(vertexBase + 2);
                mesh.indices.push_back(vertexBase + 3);
            }
        }
    }
}

} // namespace

bool TwigCardVariant::accepts(const TwigCard& card) const {
    return card.foliageDensity >= minFoliageDensity && card.foliageDensity <= maxFoliageDensity &&
           card.clusterCount >= minClusters && card.clusterCount <= maxClusters &&
           card.crownDepth >= minCrownDepth && card.crownDepth <= maxCrownDepth &&
           card.branchFraction >= minBranchFraction && card.branchFraction <= maxBranchFraction;
}

void buildTwigCards(const StemGraph& graph, const std::vector<FoliageCluster>& clusters,
                    const std::vector<Vec3>& tipPositions, const TwigCardParameters& parameters,
                    std::vector<TwigCard>& cards) {
    cards.clear();
    const std::size_t count = graph.internodeCount();
    if (count == 0 || clusters.empty() || tipPositions.size() < count) {
        return;
    }

    // Assign every thin internode to a card: a run of branchlets starts a card
    // where the wood first becomes thin enough, and starts another once the run
    // is longer than one photographed twig. Radii fall tipward, so a run below
    // a card base stays thin. One index-order pass; parents precede children.
    std::vector<Index> cardOf(count, kInvalidIndex);
    std::vector<std::uint32_t> slotOf(count, 0);
    std::vector<float> runLength(count, 0.0f);
    std::vector<float> stemLength; // per card, summed over every internode on it
    for (Index i = 0; i < static_cast<Index>(count); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.radius > parameters.maxBearingRadius) {
            continue;
        }
        const Index inherited = node.parent == kInvalidIndex ? kInvalidIndex : cardOf[node.parent];
        const float upstream = node.parent == kInvalidIndex ? 0.0f : runLength[node.parent];
        if (inherited != kInvalidIndex && upstream < parameters.maxLength) {
            cardOf[i] = inherited;
            slotOf[i] = slotOf[inherited];
            runLength[i] = upstream + node.length;
            stemLength[slotOf[inherited]] += node.length;
            continue;
        }
        cardOf[i] = i; // starts its own card
        slotOf[i] = static_cast<std::uint32_t>(cards.size());
        runLength[i] = node.length;
        stemLength.push_back(node.length);
        TwigCard card;
        card.twigBase = i;
        card.base = node.parent == kInvalidIndex ? Vec3{} : tipPositions[node.parent];
        card.axis = node.direction.normalizedOr(kUpAxis);
        cards.push_back(card);
    }
    if (cards.empty()) {
        return;
    }

    // Size each card to the foliage it absorbs: reach along the twig axis, and
    // the mean cluster offset for a stable orientation. Crown extents come from
    // the clusters rather than from the stems, so the top is the highest
    // foliage and the base the lowest, matching the growth model's crown depth.
    std::vector<Vec3> clusterSum(cards.size(), Vec3{});
    float crownTop = -1.0e30f;
    float crownBase = 1.0e30f;
    for (const FoliageCluster& cluster : clusters) {
        if (cluster.internode >= count) {
            continue;
        }
        crownTop = std::max(crownTop, cluster.position.z);
        crownBase = std::min(crownBase, cluster.position.z);
        const Index base = cardOf[cluster.internode];
        if (base == kInvalidIndex) {
            continue; // cluster on wood too thick to be carded
        }
        TwigCard& card = cards[slotOf[base]];
        const Vec3 offset = cluster.position - card.base;
        clusterSum[slotOf[base]] += offset;
        card.height = std::max(card.height, offset.length() + cluster.scale);
        ++card.clusterCount;
    }
    const float crownSpan = crownTop - crownBase;
    const BoughPositions boughs = computeBoughPositions(graph);

    std::size_t kept = 0;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        TwigCard& card = cards[i];
        if (card.clusterCount == 0) {
            continue; // leafless twig: nothing to stand in for
        }
        card.axis = clusterSum[i].normalizedOr(card.axis);
        card.height = std::clamp(card.height, parameters.minHeight, parameters.maxLength) *
                      parameters.sizeScale;
        card.width = card.height * parameters.aspect;
        card.foliageDensity =
            stemLength[i] > 0.0f
                ? static_cast<float>(card.clusterCount) * parameters.clusterSpacing / stemLength[i]
                : 0.0f;
        card.crownDepth =
            crownSpan > 0.0f ? std::clamp((crownTop - card.base.z) / crownSpan, 0.0f, 1.0f) : 0.0f;
        card.branchFraction = boughs.fractionAt(graph, card.twigBase);
        auto stream = RandomStream::derive(parameters.seed, RngPurpose::Foliage,
                                           static_cast<std::uint64_t>(card.twigBase));
        card.roll = stream.uniformFloat(0.0f, 2.0f * kPi);
        card.variant =
            parameters.variants.empty() ? 0 : selectVariant(parameters.variants, card, stream);
        cards[kept] = card;
        ++kept;
    }
    cards.resize(kept);
}

void generateTwigCardMesh(const std::vector<TwigCard>& cards,
                          const TwigCardParameters& parameters, Mesh& mesh,
                          std::vector<MeshSection>* sections) {
    mesh.clear();
    const std::uint32_t quadCount = std::max(1u, parameters.quadsPerCard);
    const std::size_t quads = cards.size() * quadCount * std::max(1u, parameters.instancesPerCard);
    mesh.positions.reserve(quads * 4);
    mesh.normals.reserve(quads * 4);
    mesh.uvs.reserve(quads * 4);
    mesh.indices.reserve(quads * 6);

    const std::uint32_t materialCount =
        parameters.variants.empty() ? 1u : static_cast<std::uint32_t>(parameters.variants.size());
    if (sections != nullptr) {
        sections->clear();
        sections->reserve(materialCount);
    }

    // One pass per material rather than a sort: the material count is the size
    // of a hand-authored texture list, and iterating cards in index order
    // within each pass keeps the output identical to the ungrouped order when
    // there is only one material.
    for (std::uint32_t material = 0; material < materialCount; ++material) {
        const std::uint32_t sectionStart = static_cast<std::uint32_t>(mesh.indices.size());
        emitCardsFor(cards, parameters, material, materialCount, quadCount, mesh);
        if (sections != nullptr) {
            sections->push_back({material, sectionStart,
                                 static_cast<std::uint32_t>(mesh.indices.size()) - sectionStart});
        }
    }
}

} // namespace flora

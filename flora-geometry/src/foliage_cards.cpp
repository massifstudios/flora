#include "flora/geometry/foliage_cards.hpp"

#include <algorithm>
#include <cmath>

#include "flora/math/rotation.hpp"
#include "flora/rng/random.hpp"

namespace flora {

namespace {

constexpr float kPi = 3.1415927f;

} // namespace

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
    for (Index i = 0; i < static_cast<Index>(count); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.radius > parameters.maxBearingRadius) {
            continue;
        }
        const Index inherited = node.parent == kInvalidIndex ? kInvalidIndex : cardOf[node.parent];
        const float upstream = node.parent == kInvalidIndex ? 0.0f : runLength[node.parent];
        if (inherited != kInvalidIndex && upstream < parameters.maxLength) {
            cardOf[i] = inherited;
            runLength[i] = upstream + node.length;
            continue;
        }
        cardOf[i] = i; // starts its own card
        slotOf[i] = static_cast<std::uint32_t>(cards.size());
        runLength[i] = node.length;
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
    // the mean cluster offset for a stable orientation.
    std::vector<Vec3> clusterSum(cards.size(), Vec3{});
    for (const FoliageCluster& cluster : clusters) {
        if (cluster.internode >= count) {
            continue;
        }
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
        auto stream = RandomStream::derive(parameters.seed, RngPurpose::Foliage,
                                           static_cast<std::uint64_t>(card.twigBase));
        card.roll = stream.uniformFloat(0.0f, 2.0f * kPi);
        cards[kept] = card;
        ++kept;
    }
    cards.resize(kept);
}

void generateTwigCardMesh(const std::vector<TwigCard>& cards,
                          const TwigCardParameters& parameters, Mesh& mesh) {
    mesh.clear();
    const std::uint32_t quadCount = std::max(1u, parameters.quadsPerCard);
    mesh.positions.reserve(cards.size() * quadCount * 4);
    mesh.normals.reserve(cards.size() * quadCount * 4);
    mesh.uvs.reserve(cards.size() * quadCount * 4);
    mesh.indices.reserve(cards.size() * quadCount * 6);

    for (const TwigCard& card : cards) {
        if (card.height <= 0.0f || card.width <= 0.0f) {
            continue;
        }
        const Vec3 axis = card.axis.normalizedOr(kUpAxis);
        const Vec3 top = axis * card.height;
        const float leftEdge = -parameters.pivotU * card.width;
        const float rightEdge = leftEdge + card.width;

        for (std::uint32_t quad = 0; quad < quadCount; ++quad) {
            const float roll =
                card.roll + kPi * static_cast<float>(quad) / static_cast<float>(quadCount);
            const Vec3 side = rotateAroundAxis(perpendicularTo(axis), axis, roll);
            const Vec3 normal = side.cross(axis).normalizedOr(kUpAxis);

            const std::uint32_t vertexBase = static_cast<std::uint32_t>(mesh.positions.size());
            mesh.positions.push_back(card.base + side * leftEdge);
            mesh.positions.push_back(card.base + side * rightEdge);
            mesh.positions.push_back(card.base + side * rightEdge + top);
            mesh.positions.push_back(card.base + side * leftEdge + top);
            for (int corner = 0; corner < 4; ++corner) {
                mesh.normals.push_back(normal);
            }
            mesh.uvs.push_back({0.0f, 0.0f});
            mesh.uvs.push_back({1.0f, 0.0f});
            mesh.uvs.push_back({1.0f, 1.0f});
            mesh.uvs.push_back({0.0f, 1.0f});

            mesh.indices.push_back(vertexBase);
            mesh.indices.push_back(vertexBase + 1);
            mesh.indices.push_back(vertexBase + 2);
            mesh.indices.push_back(vertexBase);
            mesh.indices.push_back(vertexBase + 2);
            mesh.indices.push_back(vertexBase + 3);
        }
    }
}

} // namespace flora

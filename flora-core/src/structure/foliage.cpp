#include "flora/structure/foliage.hpp"

#include <algorithm>

namespace flora {

void computeFoliagePlacement(const StemGraph& graph, const GrowthParameters& parameters,
                             const std::vector<Vec3>& tipPositions,
                             FoliagePlacementScratch& scratch,
                             std::vector<FoliageCluster>& clusters) {
    clusters.clear();
    const std::size_t internodeCount = graph.internodeCount();
    if (internodeCount == 0) {
        return;
    }

    // Stem path to the nearest live tip beyond each internode. Children always
    // have higher indices, so one descending pass resolves it: by the time an
    // internode is reached its children have folded in, and one still holding
    // the sentinel is itself a tip.
    constexpr float kNoTip = 1.0e30f;
    std::vector<float>& tipDistance = scratch.tipDistance;
    tipDistance.assign(internodeCount, kNoTip);
    for (std::size_t i = internodeCount; i-- > 0;) {
        const Index index = static_cast<Index>(i);
        const Internode& node = graph.internode(index);
        if (!node.alive) {
            continue;
        }
        if (tipDistance[index] == kNoTip) {
            tipDistance[index] = 0.0f;
        }
        if (node.parent != kInvalidIndex) {
            float& upstream = tipDistance[node.parent];
            upstream = std::min(upstream, tipDistance[index] + node.length);
        }
    }

    // Distance along the axis path since the last cluster; roots start
    // eligible. Parents precede children in index order.
    constexpr float kStartEligible = 1.0e30f;
    std::vector<float>& distanceScratch = scratch.spacing;
    distanceScratch.assign(internodeCount, kStartEligible);

    const Vec3 droopPull = Vec3{0.0f, 0.0f, -parameters.leafDroop};
    for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
        const Internode& node = graph.internode(i);
        float distance = kStartEligible;
        if (node.parent != kInvalidIndex) {
            distance = distanceScratch[node.parent] + node.length;
        }
        distanceScratch[i] = distance;

        if (!node.alive || node.radius > parameters.leafMaxBearingRadius ||
            tipDistance[i] > parameters.leafBearingTipDistance ||
            distance < parameters.leafClusterSpacing) {
            continue;
        }
        FoliageCluster cluster;
        cluster.internode = i;
        cluster.position = tipPositions[i];
        cluster.direction = (node.direction + droopPull).normalizedOr(node.direction);
        cluster.scale = parameters.leafClusterScale;
        clusters.push_back(cluster);
        distanceScratch[i] = 0.0f;
    }
}

void computeFoliagePlacement(const StemGraph& graph, const GrowthParameters& parameters,
                             const std::vector<Vec3>& tipPositions,
                             std::vector<FoliageCluster>& clusters) {
    FoliagePlacementScratch scratch;
    computeFoliagePlacement(graph, parameters, tipPositions, scratch, clusters);
}

} // namespace flora

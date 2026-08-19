#include "flora/structure/foliage.hpp"

namespace flora {

void computeFoliagePlacement(const StemGraph& graph, const GrowthParameters& parameters,
                             const std::vector<Vec3>& tipPositions,
                             std::vector<float>& distanceScratch,
                             std::vector<FoliageCluster>& clusters) {
    clusters.clear();
    const std::size_t internodeCount = graph.internodeCount();
    if (internodeCount == 0) {
        return;
    }

    // Distance along the axis path since the last cluster; roots start
    // eligible. Parents precede children in index order.
    constexpr float kStartEligible = 1.0e30f;
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
    std::vector<float> distanceScratch;
    computeFoliagePlacement(graph, parameters, tipPositions, distanceScratch, clusters);
}

} // namespace flora

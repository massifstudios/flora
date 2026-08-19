#pragma once

#include <vector>

#include "flora/growth/parameters.hpp"
#include "flora/structure/stem_graph.hpp"

namespace flora {

// Foliage is derived data recomputed from structure (leaves regrow every
// season); only the placement rule's parameters are simulation state. A
// cluster stands for an instanced group of leaves, never per-leaf simulation
// (ARCHITECTURE.md §2).
struct FoliageCluster {
    Index internode = kInvalidIndex; // bearing internode
    Vec3 position;                   // internode tip, plant-local
    Vec3 direction = kUpAxis;        // droop-adjusted growth direction
    float scale = 0.0f;              // cluster radius, meters
};

// Deterministic single pass in index order, O(internodes): live internodes
// with radius <= leafMaxBearingRadius bear clusters, at least
// leafClusterSpacing apart along the axis path (spacing carries across
// junctions). tipPositions must come from computeTipPositions on the same
// graph. distanceScratch is caller-owned so per-step calls allocate nothing.
void computeFoliagePlacement(const StemGraph& graph, const GrowthParameters& parameters,
                             const std::vector<Vec3>& tipPositions,
                             std::vector<float>& distanceScratch,
                             std::vector<FoliageCluster>& clusters);

// Convenience overload for tools and tests; allocates its own scratch.
void computeFoliagePlacement(const StemGraph& graph, const GrowthParameters& parameters,
                             const std::vector<Vec3>& tipPositions,
                             std::vector<FoliageCluster>& clusters);

} // namespace flora

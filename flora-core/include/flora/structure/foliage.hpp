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

// Caller-owned buffers so per-step placement allocates nothing.
struct FoliagePlacementScratch {
    std::vector<float> spacing;     // axis-path distance since the last cluster
    std::vector<float> tipDistance; // stem path to the nearest live tip beyond
};

// Deterministic, O(internodes) in two index-order passes: live internodes
// within leafBearingTipDistance of a tip and no thicker than
// leafMaxBearingRadius bear clusters, at least leafClusterSpacing apart along
// the axis path (spacing carries across junctions). Leaves sit on young shoots,
// so proximity to a tip is the primary rule; the radius cap only keeps them off
// stubs that a shed branch left near the surface. tipPositions must come from
// computeTipPositions on the same graph.
void computeFoliagePlacement(const StemGraph& graph, const GrowthParameters& parameters,
                             const std::vector<Vec3>& tipPositions,
                             FoliagePlacementScratch& scratch,
                             std::vector<FoliageCluster>& clusters);

// Convenience overload for tools and tests; allocates its own scratch.
void computeFoliagePlacement(const StemGraph& graph, const GrowthParameters& parameters,
                             const std::vector<Vec3>& tipPositions,
                             std::vector<FoliageCluster>& clusters);

} // namespace flora

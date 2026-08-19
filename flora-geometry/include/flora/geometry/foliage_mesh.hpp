#pragma once

#include <cstdint>
#include <vector>

#include "flora/geometry/mesh.hpp"
#include "flora/structure/foliage.hpp"

namespace flora {

struct FoliageMeshParameters {
    std::uint32_t quadsPerCluster = 2; // crossed cards around the cluster axis
    float sizeScale = 1.5f;            // card edge = cluster scale × sizeScale
    std::uint64_t seed = 0;            // deterministic per-cluster roll variation
};

// Crossed leaf cards per cluster, oriented along the cluster direction with a
// deterministic roll drawn from (seed, RngPurpose::Foliage, clusterIndex).
// Cards carry flat quad normals; renderers treat foliage as two-sided.
// O(clusters × quadsPerCluster); output order follows cluster order.
void generateFoliageMesh(const std::vector<FoliageCluster>& clusters,
                         const FoliageMeshParameters& parameters, Mesh& mesh);

} // namespace flora

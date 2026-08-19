#include "flora/geometry/foliage_mesh.hpp"

#include <algorithm>
#include <cmath>

#include "flora/math/rotation.hpp"
#include "flora/rng/random.hpp"

namespace flora {

namespace {

constexpr float kPi = 3.1415927f;

} // namespace

void generateFoliageMesh(const std::vector<FoliageCluster>& clusters,
                         const FoliageMeshParameters& parameters, Mesh& mesh) {
    mesh.clear();
    const std::uint32_t quadCount = std::max(1u, parameters.quadsPerCluster);
    mesh.positions.reserve(clusters.size() * quadCount * 4);
    mesh.normals.reserve(clusters.size() * quadCount * 4);
    mesh.uvs.reserve(clusters.size() * quadCount * 4);
    mesh.indices.reserve(clusters.size() * quadCount * 6);

    for (std::size_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex) {
        const FoliageCluster& cluster = clusters[clusterIndex];
        const float halfEdge = 0.5f * cluster.scale * parameters.sizeScale;
        if (halfEdge <= 0.0f) {
            continue;
        }
        const Vec3 axis = cluster.direction.normalizedOr(kUpAxis);
        auto stream = RandomStream::derive(parameters.seed, RngPurpose::Foliage,
                                           static_cast<std::uint64_t>(clusterIndex));
        const float baseRoll = stream.uniformFloat(0.0f, 2.0f * kPi);
        // Cards extend tipward from the bearing twig.
        const Vec3 center = cluster.position + axis * halfEdge;

        for (std::uint32_t quad = 0; quad < quadCount; ++quad) {
            const float roll =
                baseRoll + kPi * static_cast<float>(quad) / static_cast<float>(quadCount);
            const Vec3 side = rotateAroundAxis(perpendicularTo(axis), axis, roll);
            const Vec3 quadNormal = side.cross(axis).normalizedOr(kUpAxis);

            const std::uint32_t vertexBase = static_cast<std::uint32_t>(mesh.positions.size());
            const Vec3 tangent = side * halfEdge;
            const Vec3 along = axis * halfEdge;
            mesh.positions.push_back(center - tangent - along);
            mesh.positions.push_back(center + tangent - along);
            mesh.positions.push_back(center + tangent + along);
            mesh.positions.push_back(center - tangent + along);
            for (int corner = 0; corner < 4; ++corner) {
                mesh.normals.push_back(quadNormal);
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

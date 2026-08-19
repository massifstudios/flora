#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "flora/math/vec2.hpp"
#include "flora/math/vec3.hpp"

namespace flora {

// Disposable render geometry derived from simulation structure
// (ARCHITECTURE.md §9). Triangle list; parallel per-vertex arrays.
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<std::uint32_t> indices;

    std::size_t vertexCount() const { return positions.size(); }
    std::size_t triangleCount() const { return indices.size() / 3; }

    void clear() {
        positions.clear();
        normals.clear();
        uvs.clear();
        indices.clear();
    }
};

// Checks finiteness, array-size agreement, unit normals, index validity, and
// triangle-list alignment. Appends violation descriptions to errors when given.
bool validateMesh(const Mesh& mesh, std::string* errors = nullptr);

// Axis-aligned bounds; returns false for an empty mesh.
bool computeBounds(const Mesh& mesh, Vec3& minimum, Vec3& maximum);

} // namespace flora

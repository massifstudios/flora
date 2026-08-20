#pragma once

#include <cstdint>
#include <vector>

#include "flora/math/vec3.hpp"

namespace flora {

// Self-shading field: a dense voxel grid of foliage density used as the
// occlusion medium for the raycast light backend.
//
// Each caster (a leafy cluster) deposits shadowIntensity into its own cell;
// shade is not propagated through the grid. Light at a position is integrated
// by marching hemisphere rays through the field (transmittanceAlong), so
// occlusion follows the sky direction a shoot actually sees rather than a
// fixed downward stencil.
//
// Complexity: build O(casters), ray march O(cells along the ray). Rebuilt from
// structure every step; cells are a single reused allocation.
//
// Determinism: bounds and accumulation run in caller-supplied index order on
// one thread. Parallel builds must partition casters and reduce in fixed
// order (ARCHITECTURE.md §8).
class ShadowVoxelGrid {
public:
    struct Settings {
        float voxelSize = 0.5f;       // meters; enlarged when bounds exceed the cap
        float shadowIntensity = 0.2f; // foliage density one caster adds to its own cell
        std::uint32_t maxCellsPerAxis = 192; // memory guard
    };

    // Rebuilds the field over the casters' bounds (plus one voxel of margin).
    void build(const std::vector<Vec3>& casterPositions, const Settings& settings);

    // Foliage density at a position; 0 outside the grid or before build().
    float densityAt(const Vec3& position) const;

    // Transmittance (0..1) along a ray from position in unit direction,
    // marching cells until the grid is left. Absorption per unit density per
    // meter is absorptionScale. The starting cell is skipped so a caster does
    // not block its own rays.
    float transmittanceAlong(const Vec3& position, const Vec3& direction,
                             float absorptionScale) const;

    bool empty() const { return cells_.empty(); }
    float voxelSize() const { return voxelSize_; }
    std::size_t cellCount() const { return cells_.size(); }

private:
    bool cellIndex(const Vec3& position, std::int32_t& cellX, std::int32_t& cellY,
                   std::int32_t& cellZ) const;
    float cellAt(std::int32_t cellX, std::int32_t cellY, std::int32_t cellZ) const;

    Vec3 origin_;                 // minimum corner of cell (0,0,0)
    float voxelSize_ = 0.5f;
    std::int32_t countX_ = 0;
    std::int32_t countY_ = 0;
    std::int32_t countZ_ = 0;
    std::vector<float> cells_;
};

} // namespace flora

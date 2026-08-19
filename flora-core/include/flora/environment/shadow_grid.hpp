#pragma once

#include <cstdint>
#include <vector>

#include "flora/math/vec3.hpp"

namespace flora {

// Self-shading field: shadow propagation on a dense voxel grid
// (Palubicki et al. 2009, "Self-organizing tree models for image synthesis";
// classified APPROXIMATE, the L2 structural-tree light backend).
//
// Each caster (a leafy shoot tip) adds shadowIntensity / shadowFalloff^q to
// the voxels of an inverted pyramid below it: layer q spans (2q+1)^2 voxels,
// q = 0..shadowDepth. Queries read one cell; a caster shades its own voxel,
// so callers subtract one shadowIntensity when sampling at a caster.
//
// Complexity: build O(casters × shadowDepth^3), query O(1). Rebuilt from
// structure every step; cells are a single reused allocation.
//
// Determinism: bounds and accumulation run in caller-supplied index order on
// one thread. Parallel builds must partition casters and reduce in fixed
// order (ARCHITECTURE.md §8).
class ShadowVoxelGrid {
public:
    // Pyramids reach only shadowDepth voxels down, which is metres, not crown
    // heights: under a tall crown the lower branches stay lit and the tree
    // never lifts its crown. VerticalColumn instead carries shade all the way
    // down each column with Beer-Lambert extinction, in O(cells) rather than
    // O(casters × depth³).
    enum class Mode : std::uint8_t {
        Pyramid,
        VerticalColumn,
    };

    struct Settings {
        Mode mode = Mode::Pyramid;
        float voxelSize = 0.5f;        // meters; enlarged when bounds exceed the cap
        float shadowIntensity = 0.2f;  // shadow added by one caster to its own voxel
        float shadowFalloff = 2.0f;    // per-layer divisor, > 1 (Pyramid)
        std::uint32_t shadowDepth = 5; // pyramid layers below the caster
        // VerticalColumn: fraction of shade surviving one meter further down,
        // and how far shade spreads sideways per meter of descent (a crown
        // shades slightly wider than its own footprint).
        float verticalRetention = 0.86f;
        float lateralSpread = 0.25f;
        std::uint32_t maxCellsPerAxis = 192; // memory guard
    };

    // Rebuilds the field over the casters' bounds (plus shadowDepth margin).
    void build(const std::vector<Vec3>& casterPositions, const Settings& settings);

    // Accumulated shadow at a position; 0 outside the grid or before build().
    float shadowAt(const Vec3& position) const;

    // Unit direction from a position toward falling shadow (6-neighbor
    // gradient), or fallback where the field is flat. Shaded shoots grow
    // toward gaps in the canopy.
    Vec3 openDirection(const Vec3& position, const Vec3& fallback) const;

    // Transmittance (0..1) along a ray from position in unit direction,
    // marching cells until the grid is left. Absorption per unit shadow per
    // meter is absorptionScale. The starting cell is skipped so a caster does
    // not block its own rays. Used by the L3 raycast backend.
    float transmittanceAlong(const Vec3& position, const Vec3& direction,
                             float absorptionScale) const;

    bool empty() const { return cells_.empty(); }
    float voxelSize() const { return voxelSize_; }
    std::size_t cellCount() const { return cells_.size(); }

private:
    void buildVerticalColumns(const std::vector<Vec3>& casterPositions, const Settings& settings);
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

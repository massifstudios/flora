#include "flora/environment/shadow_grid.hpp"

#include <algorithm>
#include <cmath>

namespace flora {

namespace {

constexpr float kHugeBound = 1.0e30f;

} // namespace

void ShadowVoxelGrid::build(const std::vector<Vec3>& casterPositions, const Settings& settings) {
    cells_.clear();
    countX_ = countY_ = countZ_ = 0;
    if (casterPositions.empty()) {
        return;
    }

    Vec3 minimum{kHugeBound, kHugeBound, kHugeBound};
    Vec3 maximum{-kHugeBound, -kHugeBound, -kHugeBound};
    for (const Vec3& position : casterPositions) {
        minimum.x = std::min(minimum.x, position.x);
        minimum.y = std::min(minimum.y, position.y);
        minimum.z = std::min(minimum.z, position.z);
        maximum.x = std::max(maximum.x, position.x);
        maximum.y = std::max(maximum.y, position.y);
        maximum.z = std::max(maximum.z, position.z);
    }

    const float margin = settings.voxelSize;
    minimum = minimum - Vec3{margin, margin, margin};
    maximum = maximum + Vec3{margin, margin, margin};

    voxelSize_ = settings.voxelSize;
    const std::int32_t axisCap = static_cast<std::int32_t>(std::max(8u, settings.maxCellsPerAxis));
    const Vec3 extent = maximum - minimum;
    const float largestExtent = std::max({extent.x, extent.y, extent.z});
    if (largestExtent > voxelSize_ * static_cast<float>(axisCap)) {
        voxelSize_ = largestExtent / static_cast<float>(axisCap); // coarser, never larger memory
    }

    origin_ = minimum;
    countX_ = static_cast<std::int32_t>(extent.x / voxelSize_) + 1;
    countY_ = static_cast<std::int32_t>(extent.y / voxelSize_) + 1;
    countZ_ = static_cast<std::int32_t>(extent.z / voxelSize_) + 1;
    cells_.assign(static_cast<std::size_t>(countX_) * static_cast<std::size_t>(countY_) *
                      static_cast<std::size_t>(countZ_),
                  0.0f);

    for (const Vec3& position : casterPositions) {
        std::int32_t casterX = 0;
        std::int32_t casterY = 0;
        std::int32_t casterZ = 0;
        if (!cellIndex(position, casterX, casterY, casterZ)) {
            continue; // cannot happen for build inputs; guards queries-after-move
        }
        cells_[(static_cast<std::size_t>(casterZ) * static_cast<std::size_t>(countY_) +
                static_cast<std::size_t>(casterY)) *
                   static_cast<std::size_t>(countX_) +
               static_cast<std::size_t>(casterX)] += settings.shadowIntensity;
    }
}

bool ShadowVoxelGrid::cellIndex(const Vec3& position, std::int32_t& cellX, std::int32_t& cellY,
                                std::int32_t& cellZ) const {
    if (cells_.empty()) {
        return false;
    }
    const Vec3 local = position - origin_;
    cellX = static_cast<std::int32_t>(std::floor(local.x / voxelSize_));
    cellY = static_cast<std::int32_t>(std::floor(local.y / voxelSize_));
    cellZ = static_cast<std::int32_t>(std::floor(local.z / voxelSize_));
    return cellX >= 0 && cellX < countX_ && cellY >= 0 && cellY < countY_ && cellZ >= 0 &&
           cellZ < countZ_;
}

float ShadowVoxelGrid::cellAt(std::int32_t cellX, std::int32_t cellY, std::int32_t cellZ) const {
    if (cellX < 0 || cellX >= countX_ || cellY < 0 || cellY >= countY_ || cellZ < 0 ||
        cellZ >= countZ_) {
        return 0.0f;
    }
    return cells_[(static_cast<std::size_t>(cellZ) * static_cast<std::size_t>(countY_) +
                   static_cast<std::size_t>(cellY)) *
                      static_cast<std::size_t>(countX_) +
                  static_cast<std::size_t>(cellX)];
}

float ShadowVoxelGrid::densityAt(const Vec3& position) const {
    std::int32_t cellX = 0;
    std::int32_t cellY = 0;
    std::int32_t cellZ = 0;
    if (!cellIndex(position, cellX, cellY, cellZ)) {
        return 0.0f;
    }
    return cellAt(cellX, cellY, cellZ);
}

float ShadowVoxelGrid::transmittanceAlong(const Vec3& position, const Vec3& direction,
                                          float absorptionScale) const {
    std::int32_t cellX = 0;
    std::int32_t cellY = 0;
    std::int32_t cellZ = 0;
    if (!cellIndex(position, cellX, cellY, cellZ) || absorptionScale <= 0.0f) {
        return 1.0f;
    }

    // DDA cell walk (Amanatides & Woo). The starting cell is skipped: a
    // caster must not occlude its own rays.
    const std::int32_t stepX = direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0);
    const std::int32_t stepY = direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0);
    const std::int32_t stepZ = direction.z > 0.0f ? 1 : (direction.z < 0.0f ? -1 : 0);

    const auto boundaryDistance = [&](float positionAxis, float originAxis, std::int32_t cell,
                                      std::int32_t step, float directionAxis) {
        if (step == 0) {
            return kHugeBound;
        }
        const float edge =
            originAxis + (static_cast<float>(cell) + (step > 0 ? 1.0f : 0.0f)) * voxelSize_;
        return (edge - positionAxis) / directionAxis;
    };

    float nextX = boundaryDistance(position.x, origin_.x, cellX, stepX, direction.x);
    float nextY = boundaryDistance(position.y, origin_.y, cellY, stepY, direction.y);
    float nextZ = boundaryDistance(position.z, origin_.z, cellZ, stepZ, direction.z);
    const float deltaX = stepX != 0 ? voxelSize_ / std::fabs(direction.x) : kHugeBound;
    const float deltaY = stepY != 0 ? voxelSize_ / std::fabs(direction.y) : kHugeBound;
    const float deltaZ = stepZ != 0 ? voxelSize_ / std::fabs(direction.z) : kHugeBound;

    // Advance first, then accumulate: the starting cell is never counted.
    // Each entered cell contributes one voxel-length of path; the exact
    // per-cell segment length matters little for a soft shade estimate.
    float opticalDepth = 0.0f;
    while (true) {
        if (nextX <= nextY && nextX <= nextZ) {
            nextX += deltaX;
            cellX += stepX;
        } else if (nextY <= nextZ) {
            nextY += deltaY;
            cellY += stepY;
        } else {
            nextZ += deltaZ;
            cellZ += stepZ;
        }
        if (cellX < 0 || cellX >= countX_ || cellY < 0 || cellY >= countY_ || cellZ < 0 ||
            cellZ >= countZ_) {
            break;
        }
        opticalDepth += cellAt(cellX, cellY, cellZ) * voxelSize_;
    }
    return std::exp(-absorptionScale * opticalDepth);
}

} // namespace flora

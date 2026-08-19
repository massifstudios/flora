#pragma once

#include <cmath>

#include "flora/math/vec3.hpp"

namespace flora {

// Stable perpendicular: picks the world axis least aligned with the input so
// the result never degenerates and is deterministic for equal inputs.
inline Vec3 perpendicularTo(const Vec3& direction) {
    const float absX = std::fabs(direction.x);
    const float absY = std::fabs(direction.y);
    const float absZ = std::fabs(direction.z);
    Vec3 reference{1.0f, 0.0f, 0.0f};
    if (absY <= absX && absY <= absZ) {
        reference = {0.0f, 1.0f, 0.0f};
    } else if (absZ <= absX && absZ <= absY) {
        reference = {0.0f, 0.0f, 1.0f};
    }
    return direction.cross(reference).normalizedOr({0.0f, 1.0f, 0.0f});
}

// Rodrigues rotation of v around unit axis by angle (radians).
inline Vec3 rotateAroundAxis(const Vec3& v, const Vec3& unitAxis, float angleRadians) {
    const float cosAngle = std::cos(angleRadians);
    const float sinAngle = std::sin(angleRadians);
    return v * cosAngle + unitAxis.cross(v) * sinAngle +
           unitAxis * (unitAxis.dot(v) * (1.0f - cosAngle));
}

// Direction tilted away from a unit shoot axis by tiltAngle, at rollAngle
// around the axis. Used for lateral bud placement (phyllotaxis).
inline Vec3 tiltedDirection(const Vec3& unitAxis, float rollAngleRadians, float tiltAngleRadians) {
    const Vec3 radial = rotateAroundAxis(perpendicularTo(unitAxis), unitAxis, rollAngleRadians);
    return (unitAxis * std::cos(tiltAngleRadians) + radial * std::sin(tiltAngleRadians))
        .normalizedOr(unitAxis);
}

} // namespace flora

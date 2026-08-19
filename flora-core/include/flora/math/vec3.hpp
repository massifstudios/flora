#pragma once

#include <cmath>

namespace flora {

// Canonical coordinate convention: right-handed, +Z up, meters (ARCHITECTURE.md §4).
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float xIn, float yIn, float zIn) : x(xIn), y(yIn), z(zIn) {}

    constexpr Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    constexpr Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    constexpr Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }

    constexpr Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    constexpr bool operator==(const Vec3& other) const = default;

    constexpr float dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }

    constexpr Vec3 cross(const Vec3& other) const {
        return {y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
    }

    constexpr float lengthSquared() const { return dot(*this); }

    float length() const { return std::sqrt(lengthSquared()); }

    // Returns fallback for degenerate input instead of NaN.
    Vec3 normalizedOr(const Vec3& fallback) const {
        const float squared = lengthSquared();
        if (squared <= 1.0e-20f) {
            return fallback;
        }
        const float inverseLength = 1.0f / std::sqrt(squared);
        return {x * inverseLength, y * inverseLength, z * inverseLength};
    }

    bool isFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

constexpr Vec3 operator*(float scalar, const Vec3& vector) { return vector * scalar; }

inline constexpr Vec3 kUpAxis{0.0f, 0.0f, 1.0f};

} // namespace flora

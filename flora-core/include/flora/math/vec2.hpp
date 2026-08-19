#pragma once

#include <cmath>

namespace flora {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float xIn, float yIn) : x(xIn), y(yIn) {}

    constexpr bool operator==(const Vec2& other) const = default;

    bool isFinite() const { return std::isfinite(x) && std::isfinite(y); }
};

} // namespace flora

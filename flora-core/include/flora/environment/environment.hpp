#pragma once

#include "flora/math/vec3.hpp"

namespace flora {

// External light arriving at a point before self-shading (ARCHITECTURE.md §7).
struct LightSample {
    float intensity = 1.0f; // 0..1 under an open sky; >1 allowed for boosts
    Vec3 direction = kUpAxis; // unit vector toward the dominant light
};

// Environment query interface. Backends are core-owned; adapters feed data in
// but never own queries. Sampling is per bud per step: cheap enough for
// virtual dispatch at current scales (measured in bench_light).
class Environment {
public:
    virtual ~Environment() = default;

    virtual LightSample sampleLight(const Vec3& position) const = 0;
};

// Uniform light field: every position receives the same intensity and
// direction. Reference backend for tests and determinism goldens.
class ConstantEnvironment final : public Environment {
public:
    ConstantEnvironment() = default;
    explicit ConstantEnvironment(float lightIn, const Vec3& lightDirectionIn = kUpAxis)
        : light(lightIn), lightDirection(lightDirectionIn) {}

    float light = 1.0f;
    Vec3 lightDirection{0.0f, 0.0f, 1.0f};

    LightSample sampleLight(const Vec3&) const override { return {light, lightDirection}; }
};

// Analytic sky: isotropic ambient dome plus one directional sun. Spatially
// uniform until terrain/horizon occlusion arrives with the forest milestones.
class SkyEnvironment final : public Environment {
public:
    float ambient = 0.4f;
    float sunIntensity = 0.6f;
    Vec3 sunDirection{0.35f, 0.0f, 0.94f}; // unit vector toward the sun

    LightSample sampleLight(const Vec3&) const override {
        const Vec3 combined = kUpAxis * ambient + sunDirection * sunIntensity;
        return {ambient + sunIntensity, combined.normalizedOr(kUpAxis)};
    }
};

} // namespace flora

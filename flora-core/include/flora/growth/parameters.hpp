#pragma once

#include <cstdint>

namespace flora {

// Growth control surface for the M2 simulation loop. Becomes the data-driven
// species schema in M4 (distributions + curves, JSON, versioned); the fields
// here define the vocabulary. Angles in radians, lengths in meters.
struct GrowthParameters {
    // Shoot growth
    float baseGrowthLength = 0.35f;    // metamer length under ideal conditions
    float baseGrowthThickness = 0.01f; // initial metamer radius
    std::uint32_t maxMetamersPerShoot = 3;

    // Resource allocation (Borchert–Honda with apical control λ)
    float apicalControl = 0.65f;       // λ: continuation/apical outlet bias, 0..1
    float vigorScale = 1.0f;           // resource per unit collected energy
    float activationThreshold = 0.45f; // minimum bud resource to grow

    // Branching / phyllotaxis
    float branchAngle = 0.7853982f;       // 45 deg
    float branchAngleJitter = 0.0872665f; // ±5 deg, sampled per lateral bud
    float phyllotaxisAngle = 2.3999632f;  // golden angle, 137.5 deg
    std::uint32_t lateralBudsPerMetamer = 1; // whorls

    // Bud lifecycle
    std::uint32_t budDormancyLimit = 3; // dormant steps before a bud dies

    // Hard caps (part of the determinism key; see ARCHITECTURE.md §14)
    std::uint32_t maxInternodes = 100000;
};

// Placeholder environment until the M6 backend interface: every live bud
// receives the same light level each step.
struct ConstantEnvironment {
    float light = 1.0f;
};

} // namespace flora

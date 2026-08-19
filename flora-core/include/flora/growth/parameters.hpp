#pragma once

#include <cstdint>

#include "flora/math/vec3.hpp"

namespace flora {

// Self-shading fidelity backend (ARCHITECTURE.md §6): Off is the reference
// path (external environment light only), VoxelPropagation the L2 structural
// default, Raycast the L3 hero backend. A simulation setting, not species
// biology: it never comes from species files.
enum class ShadeModel : std::uint8_t {
    Off,
    VoxelPropagation,
    Raycast,
};

// Growth control surface materialized per plant and step from a species
// definition (flora/species/species.hpp). Angles in radians, lengths in
// meters.
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

    // Orientation: per-metamer direction blending weights
    float gravitropism = 0.08f;  // pull toward +Z (negative droops)
    float phototropism = 0.05f;  // pull toward the per-bud light direction
    float gnarl = 0.06f;         // random per-metamer offset magnitude

    // Self-shading (phase 1). shadowIntensity/shadowFalloff are species
    // biology (foliage density); the rest are fidelity settings.
    ShadeModel shadeModel = ShadeModel::VoxelPropagation;
    float shadowIntensity = 0.2f;      // shadow one leafy tip casts on its own voxel
    float shadowFalloff = 2.0f;        // per-layer divisor, > 1
    float shadowVoxelSize = 0.5f;      // meters
    std::uint32_t shadowDepth = 5;     // pyramid layers
    std::uint32_t raycastRayCount = 32; // hemisphere rays, Raycast backend only

    // Secondary thickening: cross-section area gain per unit collected energy
    // per step (incremental pipe model)
    float thicknessGrowthFactor = 1.5e-6f;

    // Shedding: a lateral branch is shed when its collected energy divided by
    // (maintenanceCostPerVolume × subtree wood volume) falls below the ratio.
    float maintenanceCostPerVolume = 400.0f; // energy units per m^3 per step
    float dropWeakBranchRatio = 1.0f;        // 0 disables shedding
    // At most this fraction of the tree sheds per step, worst branches first.
    // Spreads correction over time instead of mass die-offs when maintenance
    // cost catches up with capped energy (stabilizer until M6 light limits
    // energy naturally).
    float maxShedFractionPerStep = 0.05f;

    // Bud lifecycle
    std::uint32_t budDormancyLimit = 3; // dormant steps before a bud dies

    // Deadwood: shed branches linger this many steps before removal
    std::uint32_t branchDecaySteps = 4;

    // Hard caps (part of the determinism key; see ARCHITECTURE.md §14)
    std::uint32_t maxInternodes = 100000;
};

} // namespace flora

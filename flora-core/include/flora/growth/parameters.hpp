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
    // Shade carried down whole columns instead of a few voxels: what lets a
    // crown shade out its own lower branches and lift. L2 default.
    VerticalExtinction,
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
    // Exponent on bud light when buds compete for resource: 1 is proportional,
    // higher values push growth into the lit part of the crown and starve the
    // shaded part. Shade-intolerant species sit high, shade-tolerant low.
    float brightGrowthPriority = 1.0f;

    // Branching / phyllotaxis
    float branchAngle = 0.7853982f;       // 45 deg
    float branchAngleJitter = 0.0872665f; // ±5 deg, sampled per lateral bud
    float phyllotaxisAngle = 2.3999632f;  // golden angle, 137.5 deg
    std::uint32_t lateralBudsPerMetamer = 1; // whorls

    // Orientation: per-metamer direction blending weights. Trunk and branches
    // are different organs here: the leader is orthotropic (chases the
    // vertical), branches are plagiotropic (hold the angle they left at and
    // sag under their own weight). One shared value turns every branch into a
    // second trunk, which is what makes a tree read as a coppiced shrub.
    float gravitropism = 0.08f;  // pull toward +Z on order-0 shoots (negative droops)
    float phototropism = 0.05f;  // pull toward the per-bud light direction
    float lateralGravitropism = -0.01f; // same, on branches (order 1 and up)
    float lateralPhototropism = 0.02f;
    float gnarl = 0.06f;         // random per-metamer offset magnitude

    // Self-shading (phase 1). shadowIntensity/shadowFalloff are species
    // biology (foliage density); the rest are fidelity settings.
    ShadeModel shadeModel = ShadeModel::VerticalExtinction;
    // Shade one leafy cluster casts: per voxel under VoxelPropagation, per
    // square meter of canopy under VerticalExtinction (which is swept at
    // canopy resolution, so its field must not depend on cell size).
    float shadowIntensity = 0.02f;
    float shadowFalloff = 2.0f;        // per-layer divisor, > 1
    float shadowVoxelSize = 0.5f;      // meters
    // The column field is swept whole every step, so it is resolved at canopy
    // scale rather than twig scale: cell count, not caster count, is its cost.
    float shadowColumnVoxelSize = 1.5f; // meters, VerticalExtinction only
    float shadowVerticalRetention = 0.86f; // shade surviving one meter of descent
    float shadowLateralSpread = 0.25f;     // sideways bleed per meter of descent
    std::uint32_t shadowDepth = 5;     // pyramid layers
    std::uint32_t raycastRayCount = 32; // hemisphere rays, Raycast backend only

    // Foliage placement (derived per step; structure/foliage.hpp). Clusters
    // are the photosynthetic producers and the shadow casters.
    // Leaves sit on young shoots. Keying that to stem path from the nearest tip
    // rather than to girth keeps leaf area independent of the thickening
    // calibration: a radius threshold has to sit just above
    // baseGrowthThickness, so any change to thickening moves the whole
    // leaf-bearing zone with it.
    float leafBearingTipDistance = 1000.0f; // meters of stem path to the nearest tip
    float leafMaxBearingRadius = 0.02f;     // upper girth cap for a bearing stem
    float leafClusterSpacing = 0.3f;    // min axis-path distance between clusters, meters
    float leafClusterScale = 0.35f;     // cluster radius, meters
    float leafDroop = 0.25f;            // downward blend of cluster orientation
    // Energy per m² of cluster cross-section per step at full light:
    // production scales with leaf area, so cluster size and spacing trade off
    // without rebalancing the whole energy budget.
    float leafProductivity = 7.0f;

    // Hydraulic limitation (APPROXIMATE; ARCHITECTURE.md §6): water transport
    // over a long conduit path costs pressure, so extension growth fades with
    // the root-to-tip stem path length and stops at maxPathLength. This is
    // what bounds tree height and crown reach; without it extension is
    // unbounded. 0 disables the limit.
    float maxPathLength = 40.0f;   // meters of stem path a shoot can be fed over
    float hydraulicFalloff = 2.0f; // exponent: >1 keeps early growth vigorous

    // Secondary thickening: cross-section area gain per unit collected energy
    // per step (incremental pipe model)
    float thicknessGrowthFactor = 1.5e-6f;
    // Blends the thickening driver from collected production (0) to
    // apical-control-weighted allocation (1). A tree reinforces the stems it
    // feeds, not merely the ones that photosynthesize, so a suppressed lateral
    // carrying a productive tip stays slender instead of growing into a second
    // trunk.
    float thicknessApicalWeight = 0.0f;

    // Shedding: a lateral branch is shed when its collected energy divided by
    // (maintenanceCostPerVolume × subtree sapwood volume) falls below the
    // ratio. Only the outer sapwoodDepth of a stem is living, respiring tissue;
    // heartwood inside it costs nothing, so maintenance grows with girth rather
    // than with cross-section and old trunks stay affordable.
    float maintenanceCostPerVolume = 400.0f; // energy units per m^3 per step
    float sapwoodDepth = 0.05f;              // meters inward from the surface
    float dropWeakBranchRatio = 1.0f;        // 0 disables shedding
    // Minimum light for a terminal stem (no live children) to survive. Cost
    // shedding only reaches whole branches that have already grown expensive;
    // this culls the shaded new shoots that would fill a crown's interior
    // before they ever pay for themselves. 0 disables it.
    float dropShadedGrowthLight = 0.0f;
    // At most this fraction of the tree sheds per step, worst branches first.
    // Spreads correction over time instead of mass die-offs when maintenance
    // cost catches up with capped energy (stabilizer until M6 light limits
    // energy naturally).
    float maxShedFractionPerStep = 0.05f;

    // Apical control by order: every branch order extends this fraction of the
    // order above it, permanently. A branch is a subordinate organ, not just a
    // shoot that happens to be short of resource -- without the handicap any
    // lateral that collects enough light overtakes the leader and the tree
    // grows as a coppiced clump of equals. 1 removes the handicap.
    float branchVigorFalloff = 1.0f;

    // Apical dominance: lateral buds this far below their axis apex stay
    // suppressed while that apex lives, so a shoot's own year of growth cannot
    // branch and the leader keeps its head start. Losing the apex releases the
    // buds below it. 0 disables suppression (resource competition only).
    float apicalDominanceRange = 0.0f; // meters of axis path below the apex

    // Bending under self weight (APPROXIMATE; ARCHITECTURE.md §6). Each
    // internode is a cantilever loaded by the wood it carries: the deflection
    // is not tracked elastically, only the fraction that the stem grows into
    // permanently, so a limb sags as it matures and never springs back when it
    // sheds a branch. Sag shortens the load arm, so bending converges.
    float branchStiffness = 1.0e6f; // meters of arm per radian; higher is stiffer
    float bendIntegration = 0.0f;   // fraction of the year's deflection kept, 0 disables

    // Bud lifecycle
    std::uint32_t budDormancyLimit = 3; // dormant steps before a bud dies

    // Deadwood: shed branches linger this many steps before removal
    std::uint32_t branchDecaySteps = 4;

    // Hard caps (part of the determinism key; see ARCHITECTURE.md §14)
    std::uint32_t maxInternodes = 100000;
};

} // namespace flora

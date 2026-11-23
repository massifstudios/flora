#pragma once

#include <cstdint>

#include "flora/math/vec3.hpp"

namespace flora {

// Self-shading fidelity backend (ARCHITECTURE.md §6): Off is the reference
// path (external environment light only), Raycast integrates sky occlusion
// through the foliage-density grid. A simulation setting, not species biology:
// it never comes from species files.
enum class ShadeModel : std::uint8_t {
    Off,
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
    // Exponent on bud light when buds compete for resource: 1 is proportional,
    // higher values push growth into the lit part of the crown and starve the
    // shaded part. Shade-intolerant species sit high, shade-tolerant low.
    float brightGrowthPriority = 1.0f;
    // How much of an outlet's claim on the resource follows its own production
    // rather than the number of growing points it carries (branch autonomy;
    // Sprugel, Hinckley & Schaap 1991). 0 makes every branch a peer of the
    // leader; 1 cannot fund a bud that has no leaves of its own yet, so a
    // sapling extends as a bare pole instead of branching.
    float branchAutonomy = 0.5f;

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
    // Gravitropic set-point angle: the angle from vertical a branch actively
    // holds (Digby & Firn 1995). A plain +Z pull turns every branch into a
    // second trunk and a plain -Z pull sends it to the ground; a set point is
    // restoring, so a limb that sags under load or wanders off in a random
    // walk steers back to its species angle and its tip turns up again.
    // Replaces lateralGravitropism on branch shoots when strength > 0.
    float lateralSetPointAngle = 1.2217305f;   // 70 deg from +Z, mid-canopy value
    float lateralSetPointStrength = 0.0f;      // 0 disables (plain ±Z pull)
    // Set-point gradient along the crown. A whorl leaves the leader acute,
    // flattens as the crown grows past it, and droops near the crown base:
    // the set point is a property of the branch's position in the crown, not
    // of the plant. Evaluated against relative crown depth (0 at the leader
    // tip, 1 at the crown base) of the branch axis origin, so a branch keeps
    // sinking through the gradient as the tree grows over it. Negative means
    // unset: the end follows lateralSetPointAngle and the gradient is flat, so
    // a species that never mentions the gradient keeps its single-angle
    // behaviour. When the gradient is active it also sets the angle a lateral
    // bud emerges at, since frozen internode directions mean a branch can only
    // express an angle in the wood it grows after adopting it.
    float lateralSetPointAngleApex = -1.0f;
    float lateralSetPointAngleBase = -1.0f;
    float crownApexFraction = 0.15f; // acute zone ends at this crown depth
    float crownMidFraction = 0.6f;   // horizontal zone ends at this crown depth
    // Annual reorientation of existing branch wood toward the set point of the
    // crown zone it now sits in (reaction wood / differential growth). Without
    // it a bough is frozen at the angle it emerged with and the gradient can
    // only ever show up in wood grown after the whorl descended, which leaves
    // the mid canopy stuck at the emergence angle. 0 disables it.
    float setPointReorientRate = 0.0f;
    // Pendulous secondary branchlets. A primary bough is plagiotropic and holds
    // the crown set point; the branchlets it bears are not, they hang. The droop
    // factor blends the set point of every axis of order 2 and up from its crown
    // zone angle toward straight down, so a branchlet under a drooping lower
    // bough hangs harder than one under a horizontal mid-canopy bough without a
    // second gradient. 0 disables it, and branchlets keep the primary set point.
    float secondaryDroopFactor = 0.0f;
    // Fraction of the lateral bud slots on an order-1-and-up shoot that are
    // actually laid down. lateralBudsPerMetamer sizes the leader's whorl; a
    // branchlet curtain is sparser than that, and the thinning is what turns a
    // whorl ring into sub-opposite branchlets. 1 keeps every slot.
    float secondaryBranchDensity = 1.0f;
    // Bare inner length: a branchlet bud is only laid down this far out along
    // its bearing axis. Buds are laid down at the axis tip, so this is an age
    // gate expressed as length -- it keeps the inner bough bare and, because a
    // whorl near the apex has not had the years to extend that far, keeps
    // pendulous growth off the spire without a separate crown-zone rule.
    // 0 disables it (meters of axis path from the axis origin).
    float secondaryOnsetDistance = 0.0f;
    float gnarl = 0.06f;         // random per-metamer offset magnitude

    // Self-shading (phase 1). shadowIntensity is species biology (foliage
    // density); the rest are fidelity settings.
    ShadeModel shadeModel = ShadeModel::Raycast;
    // Foliage density one leafy cluster contributes to its own voxel; rays
    // are absorbed along their path through it (Beer-Lambert, so this is an
    // absorption coefficient per meter of ray, not a subtracted shade value).
    float shadowIntensity = 1.0f;
    float shadowVoxelSize = 0.5f;       // meters
    std::uint32_t raycastRayCount = 32; // hemisphere rays

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
    // Steps a lateral is exempt from cost shedding after it emerges. A shoot
    // in its first years is a bare axis: the shade rule culls the twigs it has
    // not yet clothed, which leaves it with no production at all, and the cost
    // rule then reads that as a branch worth dropping. A tree instead
    // subsidises establishing shoots from reserves and from the axis that bore
    // them, so the cost test only means anything once the branch has had the
    // seasons to build a crown of its own. 0 tests every branch immediately.
    std::uint32_t branchEstablishSteps = 0;
    // Minimum light for a terminal stem (no live children) to survive. Cost
    // shedding only reaches whole branches that have already grown expensive;
    // this culls the shaded new shoots that would fill a crown's interior
    // before they ever pay for themselves. 0 disables it.
    float dropShadedGrowthLight = 0.0f;
    // Crown-base clearance: the bottom of the bole is kept bare as a fraction
    // of current tree height. Shade shedding cannot do this job. Its test is
    // local, so a value harsh enough to strip the lowest whorls also hollows
    // out the crown interior, where the same darkness is normal. Real
    // self-pruning is positional -- the crown base rises as the tree does --
    // so the rule that clears the bole is a rule about height. 0 disables it.
    float crownBaseHeightFraction = 0.0f;
    // Length of branch wood left standing when clearance prunes a bough, so a
    // cleared bole carries broken snags instead of going abruptly smooth.
    // 0 removes the whole branch (plain shedding).
    float deadStubLength = 0.0f; // meters of branch axis from the attachment
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

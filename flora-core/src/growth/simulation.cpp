#include "flora/growth/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>

#include "flora/math/rotation.hpp"
#include "flora/rng/random.hpp"

namespace flora {

void plantSeedling(Plant& plant, const GrowthParameters& parameters) {
    assert(plant.graph.internodeCount() == 0);
    const Index root = plant.graph.addRoot(kUpAxis, parameters.baseGrowthLength,
                                           parameters.baseGrowthThickness, 0);
    plant.graph.addBud(root, BudType::Apical, kUpAxis, 0);
}

namespace {

constexpr float kPi = 3.1415927f;
constexpr float kTwoPi = 6.2831853f;
constexpr float kGoldenAngle = 2.3999632f;

Vec3 randomUnitVector(RandomStream& stream) {
    const float z = stream.uniformFloat(-1.0f, 1.0f);
    const float azimuth = stream.uniformFloat(0.0f, kTwoPi);
    const float radial = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {radial * std::cos(azimuth), radial * std::sin(azimuth), z};
}

ShadowVoxelGrid::Settings shadowSettings(const GrowthParameters& parameters) {
    ShadowVoxelGrid::Settings settings;
    settings.voxelSize = parameters.shadowVoxelSize;
    settings.shadowIntensity = parameters.shadowIntensity;
    return settings;
}

// Deterministic Fibonacci-spiral hemisphere directions (upper hemisphere,
// index-driven; no RNG so ray sets never perturb other streams).
Vec3 hemisphereRay(std::uint32_t rayIndex, std::uint32_t rayCount) {
    const float zenith =
        (static_cast<float>(rayIndex) + 0.5f) / static_cast<float>(rayCount);
    const float azimuth = kGoldenAngle * static_cast<float>(rayIndex);
    const float radial = std::sqrt(std::max(0.0f, 1.0f - zenith * zenith));
    return {radial * std::cos(azimuth), radial * std::sin(azimuth), zenith};
}

// Fraction of the ideal metamer length a shoot fed over this much stem path
// can still extend: 1 at the base, 0 at maxPathLength.
float hydraulicExtension(const GrowthParameters& parameters, float pathLength) {
    if (parameters.maxPathLength <= 0.0f) {
        return 1.0f;
    }
    const float reach = std::clamp(pathLength / parameters.maxPathLength, 0.0f, 1.0f);
    return std::max(0.0f, 1.0f - std::pow(reach, parameters.hydraulicFalloff));
}

// Restoring pull toward the gravitropic set-point angle: the target heading is
// the current heading's own azimuth raised (or lowered) to setPointAngle from
// vertical, so a branch keeps its compass bearing and corrects only its
// inclination. A shoot pointing straight up or down has no azimuth to hold and
// gets no correction that step.
Vec3 setPointPull(const Vec3& direction, float setPointAngle, float strength) {
    const Vec3 horizontal = direction - kUpAxis * direction.dot(kUpAxis);
    const Vec3 azimuth = horizontal.normalizedOr(Vec3{});
    if (azimuth.lengthSquared() <= 0.0f) {
        return Vec3{};
    }
    const Vec3 target = azimuth * std::sin(setPointAngle) + kUpAxis * std::cos(setPointAngle);
    return (target - direction) * strength;
}

// Vertical extent of the live crown for one step: the set-point gradient is
// expressed in relative crown depth, so a branch sinks through the gradient as
// the leader grows above it without any per-branch state.
struct CrownFrame {
    float topHeight = 0.0f;
    float depth = 0.0f; // top to lowest live branch attachment; 0 when no branches yet
};

// Relative crown depth of a height: 0 at the leader tip, 1 at the crown base.
float crownDepthOf(const CrownFrame& crown, float height) {
    if (crown.depth <= 0.0f) {
        return 0.0f;
    }
    return std::clamp((crown.topHeight - height) / crown.depth, 0.0f, 1.0f);
}

// Gravitropic set-point angle along the crown (Picea-style angular gradient):
// an acute plateau over the apex zone, a horizontal plateau over the mid
// canopy, and a droop ramp to the crown base. The apex-to-horizontal ramp
// occupies the first half of the mid zone so the profile stays continuous —
// a real crown has no angular discontinuity between whorls.
float setPointAngleAt(const GrowthParameters& parameters, float crownDepth) {
    const float apexEnd = std::clamp(parameters.crownApexFraction, 0.0f, 1.0f);
    const float midEnd = std::clamp(parameters.crownMidFraction, apexEnd, 1.0f);
    const float midAngle = parameters.lateralSetPointAngle;
    const float apexAngle = parameters.lateralSetPointAngleApex < 0.0f
                                ? midAngle
                                : parameters.lateralSetPointAngleApex;
    const float baseAngle = parameters.lateralSetPointAngleBase < 0.0f
                                ? midAngle
                                : parameters.lateralSetPointAngleBase;

    if (crownDepth <= apexEnd) {
        return apexAngle;
    }
    const float rampEnd = apexEnd + 0.5f * (midEnd - apexEnd);
    if (crownDepth < rampEnd) {
        const float t = (crownDepth - apexEnd) / (rampEnd - apexEnd);
        return apexAngle + (midAngle - apexAngle) * t;
    }
    if (crownDepth <= midEnd || midEnd >= 1.0f) {
        return midAngle;
    }
    const float t = (crownDepth - midEnd) / (1.0f - midEnd);
    return midAngle + (baseAngle - midAngle) * t;
}

// True when the species actually grades the set point along the crown, rather
// than holding one angle everywhere.
bool crownGradientActive(const GrowthParameters& parameters) {
    return parameters.lateralSetPointStrength > 0.0f &&
           (parameters.lateralSetPointAngleApex >= 0.0f ||
            parameters.lateralSetPointAngleBase >= 0.0f);
}

bool pendulousSecondaries(const GrowthParameters& parameters) {
    return parameters.lateralSetPointStrength > 0.0f && parameters.secondaryDroopFactor > 0.0f;
}

// A branchlet is not a small bough. Axes of order 2 and up leave the crown set
// point and hang: their target is blended from the zone angle of the crown
// position they hang in toward straight down, so the curtain under a drooping
// lower bough hangs harder than the one under a horizontal mid-canopy bough
// without a second gradient to tune.
float pendulousAngleFrom(const GrowthParameters& parameters, float zoneAngle) {
    const float droop = std::clamp(parameters.secondaryDroopFactor, 0.0f, 1.0f);
    return zoneAngle + (kPi - zoneAngle) * droop;
}

// Set-point angle an axis of this order holds at this crown depth.
float setPointFor(const GrowthParameters& parameters, std::uint16_t axisOrder, float crownDepth) {
    const float zoneAngle = setPointAngleAt(parameters, crownDepth);
    return axisOrder >= 2 && pendulousSecondaries(parameters)
               ? pendulousAngleFrom(parameters, zoneAngle)
               : zoneAngle;
}

// Heading at a given inclination from vertical, keeping the compass bearing the
// phyllotactic roll produced. Ordinary lateral placement tilts away from the
// bearing axis, which cannot express an angle measured from vertical once that
// axis is itself horizontal or drooping — a 140 deg tilt off a horizontal bough
// points back along the bough, not at the ground.
Vec3 directionAtInclination(const Vec3& heading, const Vec3& fallbackHeading,
                            float angleFromVertical) {
    Vec3 azimuth = (heading - kUpAxis * heading.dot(kUpAxis)).normalizedOr(Vec3{});
    if (azimuth.lengthSquared() <= 0.0f) {
        azimuth =
            (fallbackHeading - kUpAxis * fallbackHeading.dot(kUpAxis)).normalizedOr(Vec3{});
    }
    if (azimuth.lengthSquared() <= 0.0f) {
        return heading;
    }
    return azimuth * std::sin(angleFromVertical) + kUpAxis * std::cos(angleFromVertical);
}

// What a prune leaves behind. A dead primary bough on the bare bole breaks off
// short and the snag stays: there is no growth around it to swallow it and no
// crown above it that ever needs the space. Higher up, and for anything finer
// than a primary bough, the wood goes entirely. What killed the branch does not
// matter, so every prune site asks this instead of inventing its own rule.
struct SnagRule {
    float stubLength = 0.0f;      // meters retained, 0 removes the branch outright
    float clearanceHeight = 0.0f; // bole below this keeps its snags
};

std::uint32_t prune(StemGraph& graph, Index internode, const SnagRule& snags,
                    const std::vector<Vec3>& tipPositions, std::uint32_t step) {
    const Internode& node = graph.internode(internode);
    const bool primaryBough = node.branchOrder == 1 && node.parent != kInvalidIndex &&
                              graph.internode(node.parent).branchOrder == 0;
    const bool onBareBole =
        primaryBough && tipPositions[node.parent].z < snags.clearanceHeight;
    return graph.breakToStub(internode, onBareBole ? snags.stubLength : 0.0f, step);
}

// Where a branch axis leaves its bearing axis, and how far along the axis the
// queried internode sits from there. The gradient is keyed to the origin, not
// to the moving tip: keying it to the tip would let a drooping branch report a
// deeper crown position each step, droop harder, and run away. Walks rootward
// to the first internode whose parent is of lower branch order; O(axis length),
// once per shoot.
struct AxisOrigin {
    float height = 0.0f;   // height of the bearing point
    float distance = 0.0f; // axis path from the bearing point to the queried tip
};

AxisOrigin axisOriginOf(const StemGraph& graph, const std::vector<Vec3>& tipPositions,
                        Index attachment) {
    AxisOrigin origin;
    Index node = attachment;
    while (true) {
        const Internode& current = graph.internode(node);
        origin.distance += current.length;
        if (current.parent == kInvalidIndex) {
            return origin;
        }
        if (graph.internode(current.parent).branchOrder != current.branchOrder) {
            origin.height =
                current.parent < tipPositions.size() ? tipPositions[current.parent].z : 0.0f;
            return origin;
        }
        node = current.parent;
    }
}

// Below this the remaining extension is not worth a metamer: the bud goes
// dormant instead of producing degenerate near-zero-length internodes.
constexpr float kMinExtensionFactor = 0.02f;

// Cap on one step's bending so a thin, heavily loaded stem cannot fold in a
// single step: real wood reaches this over years, and the load arm shrinks as
// it does.
constexpr float kMaxBendPerStep = 0.25f; // radians

// Appends a shoot of metamerCount internodes from the given bud, steering
// each metamer by tropisms and gnarl and placing lateral buds by phyllotaxis,
// then moves the bud to the new tip. Metamers shorten as the shoot extends the
// stem path. Works on indices only: addInternode and addBud may reallocate the
// underlying arrays.
void growShoot(Plant& plant, const GrowthParameters& parameters, const Vec3& lightDirection,
               Index budIndex, std::uint32_t metamerCount, float pathLength, std::uint32_t step,
               const CrownFrame& crown, const std::vector<Vec3>& tipPositions) {
    StemGraph& graph = plant.graph;

    const Bud budBefore = graph.bud(budIndex);
    Index attachment = budBefore.internode;
    Vec3 direction = budBefore.direction;
    float rollAngle = budBefore.rollAngle;
    const bool startsLateralAxis = budBefore.type == BudType::Lateral;

    auto stream = RandomStream::derive(plant.seed, RngPurpose::ShootGrowth, budIndex, step);

    // Trunk or branch decides which tropism pair steers this shoot.
    const std::uint16_t bearingOrder = graph.internode(attachment).branchOrder;
    const bool orthotropic = bearingOrder == 0 && !startsLateralAxis;
    const float gravitropism =
        orthotropic ? parameters.gravitropism : parameters.lateralGravitropism;
    const bool holdsSetPoint = !orthotropic && parameters.lateralSetPointStrength > 0.0f;
    const float phototropism =
        orthotropic ? parameters.phototropism : parameters.lateralPhototropism;
    const std::uint16_t axisOrder =
        static_cast<std::uint16_t>(bearingOrder + (startsLateralAxis ? 1 : 0));
    const float orderVigor = std::pow(parameters.branchVigorFalloff, static_cast<float>(axisOrder));

    // Whorl position in the crown, fixed for this shoot: the angle a branch
    // holds is set by where it left the leader, not by where its tip has
    // wandered to. The axis path from that bearing point gates where the
    // branchlet curtain starts.
    const bool bearsSecondaries = axisOrder >= 1;
    const bool gatesOnset = bearsSecondaries && parameters.secondaryOnsetDistance > 0.0f;
    // A shoot that starts a lateral axis *is* the origin: the walk below would
    // report the bearing axis's own origin and path instead, which on a first
    // year bough means the whole trunk.
    const AxisOrigin origin =
        startsLateralAxis
            ? AxisOrigin{attachment < tipPositions.size() ? tipPositions[attachment].z : 0.0f, 0.0f}
        : holdsSetPoint || gatesOnset ? axisOriginOf(graph, tipPositions, attachment)
                                      : AxisOrigin{};
    const float setPointAngle =
        holdsSetPoint ? setPointFor(parameters, axisOrder, crownDepthOf(crown, origin.height))
                      : 0.0f;
    float axisDistance = origin.distance;
    const bool pendulousBuds = bearsSecondaries && pendulousSecondaries(parameters);
    const float budDensity =
        bearsSecondaries ? std::clamp(parameters.secondaryBranchDensity, 0.0f, 1.0f) : 1.0f;

    // Whorl buds emerge at the set point of the height they are laid down at,
    // so a new tier leaves the leader acute and only later tiers emerge
    // flatter. Tracked as the shoot extends: new internodes have no position
    // in tipPositions yet.
    const bool gradedEmergence = crownGradientActive(parameters);
    float shootHeight = budBefore.internode < tipPositions.size()
                            ? tipPositions[budBefore.internode].z
                            : 0.0f;

    for (std::uint32_t metamer = 0; metamer < metamerCount; ++metamer) {
        // Orientation: previous heading plus tropism pulls plus a random
        // gnarl offset, renormalized. Draw order per metamer: gnarl first,
        // then one branch-angle jitter per whorl bud.
        // A preformed bud opens at the angle it was laid down at: the set point
        // steers the years after the flush, not the flush itself. Without this
        // the first metamer is pulled straight to the zone the whorl has since
        // descended into, and every tier emerges at the same angle however
        // acute the bud was.
        const bool preformedFlush = gradedEmergence && startsLateralAxis && metamer == 0;
        const Vec3 gravityPull =
            holdsSetPoint && !preformedFlush
                ? setPointPull(direction, setPointAngle, parameters.lateralSetPointStrength)
                : (holdsSetPoint ? Vec3{} : kUpAxis * gravitropism);
        const Vec3 steered = direction + gravityPull + lightDirection * phototropism +
                             randomUnitVector(stream) * parameters.gnarl;
        direction = steered.normalizedOr(direction);

        const float extension = hydraulicExtension(parameters, pathLength);
        if (extension < kMinExtensionFactor) {
            break;
        }
        const float length = parameters.baseGrowthLength * extension * orderVigor;
        pathLength += length;

        const AxisRelation relation = (metamer == 0 && startsLateralAxis)
                                          ? AxisRelation::Lateral
                                          : AxisRelation::Continuation;
        attachment = graph.addInternode(attachment, relation, direction, length,
                                        parameters.baseGrowthThickness, step);
        shootHeight += direction.z * length;
        axisDistance += length;

        const float zoneAngle = setPointAngleAt(parameters, crownDepthOf(crown, shootHeight));
        const float emergenceAngle = pendulousBuds ? pendulousAngleFrom(parameters, zoneAngle)
                                     : gradedEmergence ? zoneAngle
                                                       : parameters.branchAngle;

        rollAngle += parameters.phyllotaxisAngle;
        const float whorlSpacing = parameters.lateralBudsPerMetamer > 0
                                       ? kTwoPi / static_cast<float>(parameters.lateralBudsPerMetamer)
                                       : 0.0f;
        // Buds are laid down at the axis tip, so the bare inner length is an
        // age gate: this year's slots open only once the axis has extended
        // past it.
        const std::uint32_t budSlots =
            axisDistance >= parameters.secondaryOnsetDistance || !gatesOnset
                ? parameters.lateralBudsPerMetamer
                : 0;
        for (std::uint32_t whorl = 0; whorl < budSlots; ++whorl) {
            const float budRoll = rollAngle + whorlSpacing * static_cast<float>(whorl);
            if (budDensity < 1.0f && stream.uniformFloat(0.0f, 1.0f) >= budDensity) {
                continue; // thinned slot: a branchlet curtain is sparser than a whorl
            }
            const float tilt = emergenceAngle +
                               stream.uniformFloat(-parameters.branchAngleJitter,
                                                   parameters.branchAngleJitter);
            // Radial placement around a bough projects onto the two sides of
            // it, which is the sub-opposite arrangement a branchlet curtain
            // hangs from; the inclination is then taken from vertical.
            const Vec3 budDirection =
                pendulousBuds ? directionAtInclination(
                                    tiltedDirection(direction, budRoll, kPi * 0.5f), direction, tilt)
                              : tiltedDirection(direction, budRoll, tilt);
            const Index lateralBud = graph.addBud(attachment, BudType::Lateral, budDirection, step);
            graph.bud(lateralBud).rollAngle = budRoll;
        }
    }

    if (attachment == budBefore.internode) {
        Bud& blocked = graph.bud(budIndex); // hydraulic limit reached mid-shoot
        blocked.state = BudState::Dormant;
        blocked.creationStep = step;
        return;
    }

    Bud& bud = graph.bud(budIndex); // fresh reference: array may have moved
    bud.internode = attachment;
    bud.direction = direction;
    bud.type = BudType::Apical; // now the leader of its own axis
    bud.state = BudState::Active;
    bud.creationStep = step; // meristem renewed; dormancy clock resets
    bud.rollAngle = rollAngle;
}

} // namespace

void growStep(Plant& plant, const GrowthParameters& parameters, const Environment& environment,
              GrowthContext& context) {
    StemGraph& graph = plant.graph;
    const std::size_t internodeCount = graph.internodeCount();
    const std::size_t budCount = graph.budCount();
    const std::uint32_t step = plant.ageSteps + 1;

    // The internode cap gates shoot creation (phase 6), not the whole step:
    // shedding, thickening, and decay must continue on a full-size tree.
    if (internodeCount == 0) {
        plant.ageSteps = step;
        return;
    }

    // Phase 1 — sample environment: per-bud light and light direction.
    // CSR grouping of buds by internode is rebuilt here and reused by the
    // allocation phase.
    auto& budOffsets = context.budOffsets_;
    auto& budIndices = context.budIndices_;
    budOffsets.assign(internodeCount + 1, 0);
    for (const Bud& bud : graph.buds()) {
        if (bud.state != BudState::Dead) {
            ++budOffsets[bud.internode + 1];
        }
    }
    for (std::size_t i = 1; i <= internodeCount; ++i) {
        budOffsets[i] += budOffsets[i - 1];
    }
    budIndices.resize(budOffsets[internodeCount]);
    {
        auto& cursor = context.energy_; // reuse as temporary write cursor
        cursor.assign(internodeCount, 0.0f);
        for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
            const Bud& bud = graph.bud(budIndex);
            if (bud.state == BudState::Dead) {
                continue;
            }
            const std::uint32_t slot =
                budOffsets[bud.internode] + static_cast<std::uint32_t>(cursor[bud.internode]);
            budIndices[slot] = budIndex;
            cursor[bud.internode] += 1.0f;
        }
    }

    graph.computeTipPositions(context.tipPositions_);
    const auto& tipPositions = context.tipPositions_;

    // Live crown extent for the set-point gradient: top of the leader down to
    // the lowest surviving branch attachment, so the gradient tracks the crown
    // as self-pruning lifts its base rather than the whole stem.
    CrownFrame crown;
    {
        float lowestBranch = 0.0f;
        bool anyBranch = false;
        for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
            const Internode& node = graph.internode(i);
            if (!node.alive) {
                continue;
            }
            crown.topHeight = std::max(crown.topHeight, tipPositions[i].z);
            if (node.branchOrder == 0) {
                continue;
            }
            const float height = tipPositions[i].z;
            lowestBranch = anyBranch ? std::min(lowestBranch, height) : height;
            anyBranch = true;
        }
        crown.depth = anyBranch ? std::max(0.0f, crown.topHeight - lowestBranch) : 0.0f;
    }

    // Stem path from the root base to each internode tip: the conduit length
    // water travels to feed a bud sitting there (hydraulic limitation, phase 6).
    auto& pathLength = context.pathLength_;
    pathLength.assign(internodeCount, 0.0f);
    for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
        const Internode& node = graph.internode(i);
        const float upstream = node.parent == kInvalidIndex ? 0.0f : pathLength[node.parent];
        pathLength[i] = upstream + node.length;
    }

    // Apical dominance mask: a lateral bud is suppressed while its own axis
    // apex is alive and closer than apicalDominanceRange along the axis.
    // Structural, so it is resolved here and honoured by allocation, bud fate
    // and shoot growth alike — suppressed buds must not draw resource either.
    auto& suppressed = context.budSuppressed_;
    suppressed.assign(budCount, 0);
    if (parameters.apicalDominanceRange > 0.0f) {
        auto& distanceToApex = context.distanceToApex_;
        auto& apexAlive = context.apexAlive_;
        distanceToApex.assign(internodeCount, 0.0f);
        apexAlive.assign(internodeCount, 0);
        // Reverse index order: an internode's continuation child is resolved
        // before the internode itself.
        for (std::size_t i = internodeCount; i-- > 0;) {
            const Index index = static_cast<Index>(i);
            const Internode& node = graph.internode(index);
            if (!node.alive) {
                continue;
            }
            Index continuation = kInvalidIndex;
            for (Index child = node.firstChild; child != kInvalidIndex;
                 child = graph.internode(child).nextSibling) {
                const Internode& candidate = graph.internode(child);
                if (candidate.alive && candidate.branchOrder == node.branchOrder) {
                    continuation = child;
                    break;
                }
            }
            if (continuation == kInvalidIndex) {
                // Axis tip: dominance comes from an apex that is still
                // extending. A stalled apex releases the buds below it, which
                // is how a leader that has run out of vigour or hydraulic
                // reach turns an excurrent sapling into a decurrent crown.
                for (std::uint32_t slot = budOffsets[index]; slot < budOffsets[index + 1]; ++slot) {
                    const Bud& bud = graph.bud(budIndices[slot]);
                    if (bud.type == BudType::Apical && bud.state == BudState::Active) {
                        apexAlive[index] = 1;
                    }
                }
                continue;
            }
            distanceToApex[index] = distanceToApex[continuation] +
                                    graph.internode(continuation).length;
            apexAlive[index] = apexAlive[continuation];
        }
        for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
            const Bud& bud = graph.bud(budIndex);
            if (bud.state == BudState::Dead || bud.type != BudType::Lateral) {
                continue;
            }
            const Index bearer = bud.internode;
            suppressed[budIndex] = (apexAlive[bearer] != 0 &&
                                    distanceToApex[bearer] < parameters.apicalDominanceRange)
                                       ? 1
                                       : 0;
        }
    }

    // Foliage placement: clusters are the photosynthetic producers and the
    // shadow casters. Thick interior wood bears no leaves, so it produces
    // nothing and (under shading) starves into the shedding phase.
    computeFoliagePlacement(graph, parameters, tipPositions, context.foliageScratch_,
                            context.foliageClusters_);
    const auto& foliage = context.foliageClusters_;

    if (parameters.shadeModel != ShadeModel::Off) {
        auto& casters = context.casterPositions_;
        casters.clear();
        for (const FoliageCluster& cluster : foliage) {
            casters.push_back(cluster.position);
        }
        context.shadowGrid_.build(casters, shadowSettings(parameters));
    }

    // Shaded light and dominant light direction at one position: sky
    // irradiance attenuated by the foliage the shoot looks through.
    const auto sampleShadedLight = [&](const Vec3& position) -> LightSample {
        const LightSample base = environment.sampleLight(position);
        if (parameters.shadeModel == ShadeModel::Raycast) {
            const std::uint32_t rayCount = std::max(1u, parameters.raycastRayCount);
            float transmitted = 0.0f;
            float weightTotal = 0.0f;
            Vec3 weightedDirection{0.0f, 0.0f, 0.0f};
            for (std::uint32_t ray = 0; ray < rayCount; ++ray) {
                const Vec3 rayDirection = hemisphereRay(ray, rayCount);
                // Cosine-weighted: a patch of sky near the horizon delivers far
                // less irradiance to an upward-facing shoot than the zenith
                // does. Weighting rays equally hands a limb under the crown a
                // full sky's worth of grazing light and keeps it alive.
                const float weight = rayDirection.dot(kUpAxis);
                const float transmittance =
                    context.shadowGrid_.transmittanceAlong(position, rayDirection, 1.0f);
                transmitted += transmittance * weight;
                weightTotal += weight;
                weightedDirection += rayDirection * (transmittance * weight);
            }
            const float shaded = weightTotal > 0.0f ? transmitted / weightTotal : 0.0f;
            return {base.intensity * shaded, weightedDirection.normalizedOr(base.direction)};
        }
        return base;
    };

    auto& budLight = context.budLight_;
    auto& budLightDirection = context.budLightDirection_;
    budLight.assign(budCount, 0.0f);
    budLightDirection.assign(budCount, kUpAxis);
    for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
        const Bud& bud = graph.bud(budIndex);
        if (bud.state == BudState::Dead) {
            continue;
        }
        const LightSample sample = sampleShadedLight(tipPositions[bud.internode]);
        budLight[budIndex] = sample.intensity;
        budLightDirection[budIndex] = sample.direction;
    }

    // Phase 2 — accumulate energy and live wood volume rootward. Children
    // always have higher indices than parents, so one descending pass each.
    // Production happens at foliage clusters.
    auto& energy = context.energy_;
    energy.assign(internodeCount, 0.0f);
    for (const FoliageCluster& cluster : foliage) {
        const float leafArea = kPi * cluster.scale * cluster.scale;
        energy[cluster.internode] +=
            sampleShadedLight(cluster.position).intensity * leafArea * parameters.leafProductivity;
    }
    for (std::size_t i = internodeCount; i-- > 0;) {
        const Index parent = graph.internode(static_cast<Index>(i)).parent;
        if (parent != kInvalidIndex) {
            energy[parent] += energy[i];
        }
    }

    // Living tissue only: the sapwood shell, not the heartwood core.
    auto& woodVolume = context.woodVolume_;
    woodVolume.assign(internodeCount, 0.0f);
    for (std::size_t i = 0; i < internodeCount; ++i) {
        const Internode& node = graph.internode(static_cast<Index>(i));
        if (node.alive) {
            const float heartwoodRadius = std::max(0.0f, node.radius - parameters.sapwoodDepth);
            woodVolume[i] = kPi * (node.radius * node.radius - heartwoodRadius * heartwoodRadius) *
                            node.length;
        }
    }
    for (std::size_t i = internodeCount; i-- > 0;) {
        const Internode& node = graph.internode(static_cast<Index>(i));
        if (node.alive && node.parent != kInvalidIndex) {
            woodVolume[node.parent] += woodVolume[i];
        }
    }

    // Height of bole that self-pruning keeps bare, shared by every prune below:
    // it decides both what clearance takes and where a prune leaves a snag.
    SnagRule snags;
    snags.stubLength = parameters.deadStubLength;
    snags.clearanceHeight = parameters.crownBaseHeightFraction * crown.topHeight;

    // Phase 3a — drop shaded new growth. A terminal stem is a shoot the tree
    // has not yet committed to: if it sits in the dark it is cut before the
    // cost rule below would ever see it, which is what keeps a crown's
    // interior from silting up with unproductive twigs.
    if (parameters.dropShadedGrowthLight > 0.0f) {
        bool anyDropped = false;
        for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
            const Internode& node = graph.internode(i);
            if (!node.alive || node.parent == kInvalidIndex) {
                continue;
            }
            bool hasLiveChild = false;
            for (Index child = node.firstChild; child != kInvalidIndex;
                 child = graph.internode(child).nextSibling) {
                hasLiveChild = hasLiveChild || graph.internode(child).alive;
            }
            if (hasLiveChild) {
                continue;
            }
            float light = -1.0f; // no bud: nothing measured this stem's exposure
            for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
                light = std::max(light, budLight[budIndices[slot]]);
            }
            if (light >= 0.0f && light < parameters.dropShadedGrowthLight) {
                prune(graph, i, snags, tipPositions, step);
                anyDropped = true;
            }
        }
        if (anyDropped) {
            graph.sweepDeadBuds();
        }
    }

    // Phase 3a-bis — crown-base clearance. Self-pruning of the lower bole is a
    // positional process, not a local light balance: the crown base tracks the
    // rising tree, and the boughs below it go whatever their own exposure says.
    // Keeping it separate from the shade rule above is what lets a crown be
    // dense on the inside and still stand on a clean bole. Only whole primary
    // boughs are cleared, and each leaves a broken snag behind.
    // One whorl per step, lowest first: a tier dies out over a season, the bole
    // does not shed a band of itself at once. Clearing everything under the
    // line the first time the line moves costs a young tree most of its leaf
    // area in a single step, and it never regains the trajectory; it also
    // stacks every snag at one height instead of leaving a ladder up the bole.
    if (snags.clearanceHeight > 0.0f) {
        Index lowestWhorl = kInvalidIndex; // trunk internode bearing it
        float lowestHeight = 0.0f;
        for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
            const Internode& node = graph.internode(i);
            if (!node.alive || node.branchOrder != 1 || node.parent == kInvalidIndex ||
                graph.internode(node.parent).branchOrder != 0) {
                continue; // not a primary bough leaving the trunk
            }
            const float attachment = tipPositions[node.parent].z;
            if (attachment >= snags.clearanceHeight) {
                continue;
            }
            if (lowestWhorl == kInvalidIndex || attachment < lowestHeight) {
                lowestWhorl = node.parent;
                lowestHeight = attachment;
            }
        }
        if (lowestWhorl != kInvalidIndex) {
            for (Index child = graph.internode(lowestWhorl).firstChild; child != kInvalidIndex;
                 child = graph.internode(child).nextSibling) {
                const Internode& bough = graph.internode(child);
                if (bough.alive && bough.branchOrder == 1) {
                    prune(graph, child, snags, tipPositions, step);
                }
            }
            graph.sweepDeadBuds();
        }
    }

    // Phase 3b — shed weak laterals before any resource is spent on them:
    // production (collected energy) must cover the maintenance cost of the
    // wood it supports. Shedding is rate-limited and worst-first so cost
    // pressure produces gradual senescence, not mass die-offs.
    if (parameters.dropWeakBranchRatio > 0.0f) {
        auto& candidates = context.shedCandidates_;
        candidates.clear();
        for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
            const Internode& node = graph.internode(i);
            if (!node.alive || node.parent == kInvalidIndex) {
                continue;
            }
            if (node.branchOrder != graph.internode(node.parent).branchOrder + 1) {
                continue; // only whole lateral branches shed, never mid-axis
            }
            if (step - node.creationStep < parameters.branchEstablishSteps) {
                continue; // still establishing: too young to have paid for itself
            }
            const float cost = parameters.maintenanceCostPerVolume * woodVolume[i];
            if (cost > 0.0f && energy[i] < parameters.dropWeakBranchRatio * cost) {
                candidates.emplace_back(energy[i] / cost, i);
            }
        }
        if (!candidates.empty()) {
            std::sort(candidates.begin(), candidates.end()); // viability asc, index tie-break
            const float budgetFraction =
                std::clamp(parameters.maxShedFractionPerStep, 0.0f, 1.0f);
            std::int64_t budget = std::max<std::int64_t>(
                1, static_cast<std::int64_t>(static_cast<float>(internodeCount) * budgetFraction));
            for (const auto& [viability, index] : candidates) {
                if (budget <= 0) {
                    break;
                }
                if (!graph.internode(index).alive) {
                    continue; // an ancestor was shed first
                }
                budget -= prune(graph, index, snags, tipPositions, step);
            }
            graph.sweepDeadBuds();
        }
    }

    // Phase 4 — allocate resource tipward across surviving outlets (live child
    // internodes and live attached buds) by bias-weighted demand.
    //
    // Demand is accumulated bud light, not collected production: the two are
    // not commensurable. Production is energy per unit leaf area, so one
    // foliage cluster outweighs several buds, and a subtree that carries the
    // leaves always outbids a bare bud beside it. A sapling whose only leaves
    // sit on its leader can then never fund a lateral, so it never grows the
    // foliage that would fund one -- it extends as a bare pole instead of
    // branching. Production still sizes the pie (at the root) and still drives
    // thickening and shedding; only the split between outlets is by demand.
    auto& resource = context.resource_;
    auto& budResource = context.budResource_;
    auto& budWeight = context.budWeight_;
    auto& demand = context.demand_;
    resource.assign(internodeCount, 0.0f);
    budResource.assign(budCount, 0.0f);
    budWeight.assign(budCount, 0.0f);
    demand.assign(internodeCount, 0.0f);
    const bool weightByBrightness = parameters.brightGrowthPriority != 1.0f;
    for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
        const Bud& bud = graph.bud(budIndex);
        const float light = budLight[budIndex];
        budWeight[budIndex] =
            weightByBrightness ? std::pow(light, parameters.brightGrowthPriority) : light;
        if (bud.state != BudState::Dead && suppressed[budIndex] == 0 &&
            graph.internode(bud.internode).alive) {
            demand[bud.internode] += budWeight[budIndex];
        }
    }
    for (std::size_t i = internodeCount; i-- > 0;) {
        const Internode& node = graph.internode(static_cast<Index>(i));
        if (node.alive && node.parent != kInvalidIndex) {
            demand[node.parent] += demand[i];
        }
    }
    for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
        const Internode& node = graph.internode(i);
        if (node.alive && node.parent == kInvalidIndex) {
            resource[i] = parameters.vigorScale * energy[i];
        }
    }
    for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
        const float available = resource[i];
        if (available <= 0.0f || !graph.internode(i).alive) {
            continue;
        }
        const Internode& node = graph.internode(i);

        // Outlet weights blend two claims on the resource, each normalized
        // across this node's outlets so the two are commensurable: how many
        // growing points an outlet carries (demand) and how much it produces
        // itself (branch autonomy; Sprugel, Hinckley & Schaap 1991). Demand
        // alone makes every branch a peer of the leader, since a bud counts the
        // same wherever it sits; production alone cannot fund a bud that has no
        // leaves yet, which is every bud on a sapling.
        float demandTotal = 0.0f;
        float energyTotal = 0.0f;
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const Internode& childNode = graph.internode(child);
            if (!childNode.alive) {
                continue;
            }
            demandTotal += demand[child];
            energyTotal += energy[child];
        }
        for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
            const Index budIndex = budIndices[slot];
            if (graph.bud(budIndex).state == BudState::Dead || suppressed[budIndex] != 0) {
                continue;
            }
            demandTotal += budWeight[budIndex]; // a bare bud produces nothing yet
        }
        if (demandTotal <= 0.0f && energyTotal <= 0.0f) {
            continue;
        }
        const float autonomy = std::clamp(parameters.branchAutonomy, 0.0f, 1.0f);
        const float demandScale = demandTotal > 0.0f ? (1.0f - autonomy) / demandTotal : 0.0f;
        const float energyScale = energyTotal > 0.0f ? autonomy / energyTotal : 0.0f;
        const auto outletWeight = [&](float outletDemand, float outletEnergy) {
            return outletDemand * demandScale + outletEnergy * energyScale;
        };

        // Borchert-Honda: the split is between the continuation outlet and the
        // laterals *as a group*, v_m = v·λQ_m / (λQ_m + (1-λ)Q_l). Weighting
        // each outlet on its own instead would let a numerous enough set of
        // laterals outvote the leader and grow a second trunk.
        float mainFlux = 0.0f;
        float lateralFlux = 0.0f;
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const Internode& childNode = graph.internode(child);
            if (!childNode.alive) {
                continue;
            }
            const float weight = outletWeight(demand[child], energy[child]);
            (childNode.branchOrder == node.branchOrder ? mainFlux : lateralFlux) += weight;
        }
        for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
            const Index budIndex = budIndices[slot];
            const Bud& bud = graph.bud(budIndex);
            if (bud.state == BudState::Dead || suppressed[budIndex] != 0) {
                continue;
            }
            (bud.type == BudType::Apical ? mainFlux : lateralFlux) +=
                outletWeight(budWeight[budIndex], 0.0f);
        }

        const float lambda = parameters.apicalControl;
        const float biasedMain = lambda * mainFlux;
        const float biasedLateral = (1.0f - lambda) * lateralFlux;
        const float biasedTotal = biasedMain + biasedLateral;
        if (biasedTotal <= 0.0f) {
            continue;
        }
        const float mainShare = available * biasedMain / biasedTotal;
        const float lateralShare = available - mainShare;
        // Within each group, resource follows the same blended weight.
        const float mainScale = mainFlux > 0.0f ? mainShare / mainFlux : 0.0f;
        const float lateralScale = lateralFlux > 0.0f ? lateralShare / lateralFlux : 0.0f;

        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const Internode& childNode = graph.internode(child);
            if (!childNode.alive) {
                continue;
            }
            const bool continues = childNode.branchOrder == node.branchOrder;
            resource[child] +=
                (continues ? mainScale : lateralScale) * outletWeight(demand[child], energy[child]);
        }
        for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
            const Index budIndex = budIndices[slot];
            const Bud& bud = graph.bud(budIndex);
            if (bud.state == BudState::Dead || suppressed[budIndex] != 0) {
                continue;
            }
            const bool apical = bud.type == BudType::Apical;
            budResource[budIndex] =
                (apical ? mainScale : lateralScale) * outletWeight(budWeight[budIndex], 0.0f);
        }
    }

    // Phase 5 — bud fate: unused lateral buds abort once they outlast the
    // dormancy limit. Apical meristems are not aborted: an axis that cannot
    // afford to extend this year keeps its growing point and resumes when
    // resources return, which is what stops a mature crown from running out of
    // meristems and dissolving.
    for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
        Bud& bud = graph.bud(budIndex);
        if (bud.type == BudType::Apical) {
            continue;
        }
        if (suppressed[budIndex] != 0 && bud.state != BudState::Dead) {
            bud.state = BudState::Dormant;
            bud.creationStep = step; // latent under dominance, not aging out
            continue;
        }
        if (bud.state == BudState::Dormant && step - bud.creationStep > parameters.budDormancyLimit) {
            bud.state = BudState::Dead;
        }
    }

    // Phase 6 — shoot growth with tropism steering. Only buds that existed at
    // step start grow; additions during the loop are next step's population.
    for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
        {
            const Bud& bud = graph.bud(budIndex);
            if (bud.state == BudState::Dead || suppressed[budIndex] != 0) {
                continue;
            }
        }
        const float received = budResource[budIndex];
        if (received < parameters.activationThreshold) {
            graph.bud(budIndex).state = BudState::Dormant;
            continue;
        }
        const float budPath = pathLength[graph.bud(budIndex).internode];
        if (hydraulicExtension(parameters, budPath) < kMinExtensionFactor) {
            // Out of hydraulic reach, not starved: the meristem stays latent
            // rather than aging out, so a tree that has stopped extending keeps
            // its crown instead of losing every growing point.
            Bud& bud = graph.bud(budIndex);
            bud.state = BudState::Dormant;
            bud.creationStep = step;
            continue;
        }
        // Rounded, minimum one: a bud past the activation threshold always
        // grows. Truncation would let float noise (e.g. 0.99999994) drop a
        // metamer and silently stall an axis.
        const std::uint32_t metamerCount = std::clamp(
            static_cast<std::uint32_t>(received + 0.5f), 1u, parameters.maxMetamersPerShoot);
        if (graph.internodeCount() + metamerCount > parameters.maxInternodes) {
            graph.bud(budIndex).state = BudState::Dormant;
            continue;
        }
        growShoot(plant, parameters, budLightDirection[budIndex], budIndex, metamerCount, budPath,
                  step, crown, tipPositions);
    }

    // Phase 7 — secondary thickening: incremental pipe model. Cross-section
    // area grows with the energy collected through this internode (pre-growth
    // snapshot); new metamers from this step keep their initial radius.
    if (parameters.thicknessGrowthFactor > 0.0f) {
        const float apicalWeight = std::clamp(parameters.thicknessApicalWeight, 0.0f, 1.0f);
        for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
            Internode& node = graph.internode(i);
            if (!node.alive) {
                continue;
            }
            const float driver = energy[i] + apicalWeight * (resource[i] - energy[i]);
            if (driver <= 0.0f) {
                continue;
            }
            const float area =
                kPi * node.radius * node.radius + parameters.thicknessGrowthFactor * driver;
            node.radius = std::sqrt(area / kPi);
        }
    }

    // Phase 7a — set-point reorientation. Existing branch wood turns toward the
    // set point of the crown zone it now occupies, so a bough that emerged
    // acute at the leader flattens as the crown grows past it and droops once
    // it reaches the crown base. Frozen directions alone can only express an
    // angle in wood grown after the whorl moved, which leaves a whole tier
    // stuck at its emergence angle for life.
    if (parameters.setPointReorientRate > 0.0f &&
        (crownGradientActive(parameters) || pendulousSecondaries(parameters))) {
        graph.computeTipPositions(context.tipPositions_);
        const auto& positions = context.tipPositions_;
        auto& originHeight = context.axisOriginHeight_;
        originHeight.assign(graph.internodeCount(), 0.0f);
        // Root-to-tip: a parent's axis origin is resolved before its children,
        // so each internode inherits its axis origin in O(1).
        for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
            const Internode& node = graph.internode(i);
            if (node.parent == kInvalidIndex) {
                continue;
            }
            originHeight[i] = graph.internode(node.parent).branchOrder != node.branchOrder
                                  ? positions[node.parent].z
                                  : originHeight[node.parent];
        }
        for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
            Internode& node = graph.internode(i);
            if (!node.alive || node.branchOrder == 0) {
                continue;
            }
            const float target =
                setPointFor(parameters, node.branchOrder, crownDepthOf(crown, originHeight[i]));
            const Vec3 pull = setPointPull(node.direction, target, parameters.setPointReorientRate);
            node.direction = (node.direction + pull).normalizedOr(node.direction);
        }
    }

    // Phase 7b — bending under self weight. Each internode is a cantilever
    // carrying the wood beyond it; the deflection that the stem grows into
    // permanently is what turns straight radial branches into the sagging
    // limbs of a mature crown. Two rootward passes, no per-step allocation.
    if (parameters.bendIntegration > 0.0f && parameters.branchStiffness > 0.0f) {
        graph.computeTipPositions(context.tipPositions_);
        const auto& positions = context.tipPositions_;
        const std::size_t bearingCount = graph.internodeCount();

        auto& loadVolume = context.loadVolume_;
        auto& loadMoment = context.loadMoment_;
        loadVolume.assign(bearingCount, 0.0f);
        loadMoment.assign(bearingCount, Vec3{});
        for (Index i = 0; i < static_cast<Index>(bearingCount); ++i) {
            const Internode& node = graph.internode(i);
            if (!node.alive) {
                continue;
            }
            const Vec3 base = node.parent == kInvalidIndex ? Vec3{} : positions[node.parent];
            const float volume = kPi * node.radius * node.radius * node.length;
            loadVolume[i] = volume;
            loadMoment[i] = (base + positions[i]) * (0.5f * volume);
        }
        for (std::size_t i = bearingCount; i-- > 0;) {
            const Internode& node = graph.internode(static_cast<Index>(i));
            if (node.alive && node.parent != kInvalidIndex) {
                loadVolume[node.parent] += loadVolume[i];
                loadMoment[node.parent] += loadMoment[i];
            }
        }

        const float integration = std::clamp(parameters.bendIntegration, 0.0f, 1.0f);
        for (Index i = 0; i < static_cast<Index>(bearingCount); ++i) {
            Internode& node = graph.internode(i);
            if (!node.alive || node.radius <= 0.0f || loadVolume[i] <= 0.0f) {
                continue;
            }
            const Vec3 base = node.parent == kInvalidIndex ? Vec3{} : positions[node.parent];
            const Vec3 arm = loadMoment[i] * (1.0f / loadVolume[i]) - base;
            const Vec3 lever = arm - kUpAxis * arm.dot(kUpAxis); // gravity acts on the horizontal reach
            const float radiusSquared = node.radius * node.radius;
            const float torque = loadVolume[i] * lever.length();
            const float deflection =
                torque * node.length / (parameters.branchStiffness * radiusSquared * radiusSquared);
            const float angle = std::min(deflection, kMaxBendPerStep) * integration;
            if (angle <= 0.0f) {
                continue;
            }
            const Vec3 down = -kUpAxis;
            const Vec3 sag = down - node.direction * down.dot(node.direction);
            const Vec3 sagDirection = sag.normalizedOr(Vec3{});
            if (sagDirection.lengthSquared() <= 0.0f) {
                continue; // stem already hangs straight down: nowhere to bend
            }
            node.direction =
                (node.direction * std::cos(angle) + sagDirection * std::sin(angle))
                    .normalizedOr(node.direction);
        }
    }

    // Phase 8 — deadwood decay: shed branches disappear after the delay.
    if (graph.deadInternodeCount() > 0 && parameters.branchDecaySteps > 0) {
        graph.removeDecayed(step, parameters.branchDecaySteps);
    }

    // Phase 9 — aging.
    plant.ageSteps = step;
}

void grow(Plant& plant, const GrowthParameters& parameters, const Environment& environment,
          std::uint32_t steps, GrowthContext& context) {
    for (std::uint32_t i = 0; i < steps; ++i) {
        growStep(plant, parameters, environment, context);
    }
}

} // namespace flora

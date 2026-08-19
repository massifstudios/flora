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

// Outlet bias: apical/continuation outlets receive λ, lateral outlets 1-λ.
float outletBias(const GrowthParameters& parameters, bool continuesAxis) {
    return continuesAxis ? parameters.apicalControl : 1.0f - parameters.apicalControl;
}

Vec3 randomUnitVector(RandomStream& stream) {
    const float z = stream.uniformFloat(-1.0f, 1.0f);
    const float azimuth = stream.uniformFloat(0.0f, kTwoPi);
    const float radial = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {radial * std::cos(azimuth), radial * std::sin(azimuth), z};
}

ShadowVoxelGrid::Settings shadowSettings(const GrowthParameters& parameters, bool pyramids) {
    ShadowVoxelGrid::Settings settings;
    settings.voxelSize = parameters.shadowVoxelSize;
    settings.shadowIntensity = parameters.shadowIntensity;
    settings.shadowFalloff = parameters.shadowFalloff;
    // Raycast integrates occlusion along rays itself: casters occupy only
    // their own voxel, no downward propagation.
    settings.shadowDepth = pyramids ? parameters.shadowDepth : 0;
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

// Appends a shoot of metamerCount internodes from the given bud, steering
// each metamer by tropisms and gnarl and placing lateral buds by phyllotaxis,
// then moves the bud to the new tip. Works on indices only: addInternode and
// addBud may reallocate the underlying arrays.
void growShoot(Plant& plant, const GrowthParameters& parameters, const Vec3& lightDirection,
               Index budIndex, std::uint32_t metamerCount, std::uint32_t step) {
    StemGraph& graph = plant.graph;

    const Bud budBefore = graph.bud(budIndex);
    Index attachment = budBefore.internode;
    Vec3 direction = budBefore.direction;
    float rollAngle = budBefore.rollAngle;
    const bool startsLateralAxis = budBefore.type == BudType::Lateral;

    auto stream = RandomStream::derive(plant.seed, RngPurpose::ShootGrowth, budIndex, step);

    for (std::uint32_t metamer = 0; metamer < metamerCount; ++metamer) {
        // Orientation: previous heading plus tropism pulls plus a random
        // gnarl offset, renormalized. Draw order per metamer: gnarl first,
        // then one branch-angle jitter per whorl bud.
        const Vec3 steered = direction + kUpAxis * parameters.gravitropism +
                             lightDirection * parameters.phototropism +
                             randomUnitVector(stream) * parameters.gnarl;
        direction = steered.normalizedOr(direction);

        const AxisRelation relation = (metamer == 0 && startsLateralAxis)
                                          ? AxisRelation::Lateral
                                          : AxisRelation::Continuation;
        attachment = graph.addInternode(attachment, relation, direction,
                                        parameters.baseGrowthLength,
                                        parameters.baseGrowthThickness, step);

        rollAngle += parameters.phyllotaxisAngle;
        const float whorlSpacing = parameters.lateralBudsPerMetamer > 0
                                       ? kTwoPi / static_cast<float>(parameters.lateralBudsPerMetamer)
                                       : 0.0f;
        for (std::uint32_t whorl = 0; whorl < parameters.lateralBudsPerMetamer; ++whorl) {
            const float budRoll = rollAngle + whorlSpacing * static_cast<float>(whorl);
            const float tilt = parameters.branchAngle +
                               stream.uniformFloat(-parameters.branchAngleJitter,
                                                   parameters.branchAngleJitter);
            const Vec3 budDirection = tiltedDirection(direction, budRoll, tilt);
            const Index lateralBud = graph.addBud(attachment, BudType::Lateral, budDirection, step);
            graph.bud(lateralBud).rollAngle = budRoll;
        }
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
        context.shadowGrid_.build(
            casters,
            shadowSettings(parameters, parameters.shadeModel == ShadeModel::VoxelPropagation));
    }

    // Shaded light and dominant light direction at one position. Self
    // contribution is excluded at caster voxels (Palubicki 2009:
    // Q = max(0, C - s + a)); non-caster positions get the same allowance,
    // a negligible brightening.
    const auto sampleShadedLight = [&](const Vec3& position) -> LightSample {
        const LightSample base = environment.sampleLight(position);
        if (parameters.shadeModel == ShadeModel::VoxelPropagation) {
            const float shade = std::max(
                0.0f, context.shadowGrid_.shadowAt(position) - parameters.shadowIntensity);
            LightSample shaded;
            shaded.intensity = std::max(0.0f, base.intensity - shade);
            shaded.direction = base.direction;
            if (shade > 0.0f) {
                const Vec3 escape = context.shadowGrid_.openDirection(position, base.direction);
                shaded.direction = (base.direction + escape * std::min(shade, 1.0f))
                                       .normalizedOr(base.direction);
            }
            return shaded;
        }
        if (parameters.shadeModel == ShadeModel::Raycast) {
            const std::uint32_t rayCount = std::max(1u, parameters.raycastRayCount);
            float transmitted = 0.0f;
            Vec3 weightedDirection{0.0f, 0.0f, 0.0f};
            for (std::uint32_t ray = 0; ray < rayCount; ++ray) {
                const Vec3 rayDirection = hemisphereRay(ray, rayCount);
                const float transmittance =
                    context.shadowGrid_.transmittanceAlong(position, rayDirection, 1.0f);
                transmitted += transmittance;
                weightedDirection += rayDirection * transmittance;
            }
            return {base.intensity * (transmitted / static_cast<float>(rayCount)),
                    weightedDirection.normalizedOr(base.direction)};
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

    auto& woodVolume = context.woodVolume_;
    woodVolume.assign(internodeCount, 0.0f);
    for (std::size_t i = 0; i < internodeCount; ++i) {
        const Internode& node = graph.internode(static_cast<Index>(i));
        if (node.alive) {
            woodVolume[i] = kPi * node.radius * node.radius * node.length;
        }
    }
    for (std::size_t i = internodeCount; i-- > 0;) {
        const Internode& node = graph.internode(static_cast<Index>(i));
        if (node.alive && node.parent != kInvalidIndex) {
            woodVolume[node.parent] += woodVolume[i];
        }
    }

    // Phase 3 — shed weak laterals before any resource is spent on them:
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
                budget -= graph.markSubtreeDead(index, step);
            }
            graph.sweepDeadBuds();
        }
    }

    // Phase 4 — allocate resource tipward across surviving outlets (live
    // child internodes and live attached buds) by bias-weighted energy.
    auto& resource = context.resource_;
    auto& budResource = context.budResource_;
    resource.assign(internodeCount, 0.0f);
    budResource.assign(budCount, 0.0f);
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

        float weightSum = 0.0f;
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const Internode& childNode = graph.internode(child);
            if (childNode.alive) {
                const bool continues = childNode.branchOrder == node.branchOrder;
                weightSum += outletBias(parameters, continues) * energy[child];
            }
        }
        for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
            const Index budIndex = budIndices[slot];
            const Bud& bud = graph.bud(budIndex);
            if (bud.state != BudState::Dead) {
                weightSum += outletBias(parameters, bud.type == BudType::Apical) *
                             budLight[budIndex];
            }
        }
        if (weightSum <= 0.0f) {
            continue;
        }

        const float scale = available / weightSum;
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const Internode& childNode = graph.internode(child);
            if (childNode.alive) {
                const bool continues = childNode.branchOrder == node.branchOrder;
                resource[child] += scale * outletBias(parameters, continues) * energy[child];
            }
        }
        for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
            const Index budIndex = budIndices[slot];
            const Bud& bud = graph.bud(budIndex);
            if (bud.state != BudState::Dead) {
                budResource[budIndex] = scale *
                                        outletBias(parameters, bud.type == BudType::Apical) *
                                        budLight[budIndex];
            }
        }
    }

    // Phase 5 — bud fate: dormant buds beyond the dormancy limit die.
    for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
        Bud& bud = graph.bud(budIndex);
        if (bud.state == BudState::Dormant && step - bud.creationStep > parameters.budDormancyLimit) {
            bud.state = BudState::Dead;
        }
    }

    // Phase 6 — shoot growth with tropism steering. Only buds that existed at
    // step start grow; additions during the loop are next step's population.
    for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
        {
            const Bud& bud = graph.bud(budIndex);
            if (bud.state == BudState::Dead) {
                continue;
            }
        }
        const float received = budResource[budIndex];
        if (received < parameters.activationThreshold) {
            graph.bud(budIndex).state = BudState::Dormant;
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
        growShoot(plant, parameters, budLightDirection[budIndex], budIndex, metamerCount, step);
    }

    // Phase 7 — secondary thickening: incremental pipe model. Cross-section
    // area grows with the energy collected through this internode (pre-growth
    // snapshot); new metamers from this step keep their initial radius.
    if (parameters.thicknessGrowthFactor > 0.0f) {
        for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
            Internode& node = graph.internode(i);
            if (node.alive && energy[i] > 0.0f) {
                const float area =
                    kPi * node.radius * node.radius + parameters.thicknessGrowthFactor * energy[i];
                node.radius = std::sqrt(area / kPi);
            }
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

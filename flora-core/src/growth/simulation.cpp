#include "flora/growth/simulation.hpp"

#include <algorithm>
#include <cassert>
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

// Outlet bias: apical/continuation outlets receive λ, lateral outlets 1-λ.
float outletBias(const GrowthParameters& parameters, bool continuesAxis) {
    return continuesAxis ? parameters.apicalControl : 1.0f - parameters.apicalControl;
}

// Appends a shoot of metamerCount internodes from the given bud, placing
// lateral buds by phyllotaxis, then moves the bud to the new tip. Works on
// indices only: addInternode/addBud may reallocate the underlying arrays.
void growShoot(Plant& plant, const GrowthParameters& parameters, Index budIndex,
               std::uint32_t metamerCount, std::uint32_t step) {
    StemGraph& graph = plant.graph;

    const Bud budBefore = graph.bud(budIndex);
    Index attachment = budBefore.internode;
    const Vec3 shootDirection = budBefore.direction;
    float rollAngle = budBefore.rollAngle;
    const bool startsLateralAxis = budBefore.type == BudType::Lateral;

    auto stream = RandomStream::derive(plant.seed, RngPurpose::ShootGrowth, budIndex, step);

    for (std::uint32_t metamer = 0; metamer < metamerCount; ++metamer) {
        const AxisRelation relation = (metamer == 0 && startsLateralAxis)
                                          ? AxisRelation::Lateral
                                          : AxisRelation::Continuation;
        attachment = graph.addInternode(attachment, relation, shootDirection,
                                        parameters.baseGrowthLength,
                                        parameters.baseGrowthThickness, step);

        rollAngle += parameters.phyllotaxisAngle;
        const float whorlSpacing = parameters.lateralBudsPerMetamer > 0
                                       ? 6.2831853f / static_cast<float>(parameters.lateralBudsPerMetamer)
                                       : 0.0f;
        for (std::uint32_t whorl = 0; whorl < parameters.lateralBudsPerMetamer; ++whorl) {
            const float budRoll = rollAngle + whorlSpacing * static_cast<float>(whorl);
            const float tilt = parameters.branchAngle +
                               stream.uniformFloat(-parameters.branchAngleJitter,
                                                   parameters.branchAngleJitter);
            const Vec3 budDirection = tiltedDirection(shootDirection, budRoll, tilt);
            const Index lateralBud = graph.addBud(attachment, BudType::Lateral, budDirection, step);
            graph.bud(lateralBud).rollAngle = budRoll;
        }
    }

    Bud& bud = graph.bud(budIndex); // fresh reference: array may have moved
    bud.internode = attachment;
    bud.type = BudType::Apical; // now the leader of its own axis
    bud.state = BudState::Active;
    bud.creationStep = step; // meristem renewed; dormancy clock resets
    bud.rollAngle = rollAngle;
}

} // namespace

void growStep(Plant& plant, const GrowthParameters& parameters,
              const ConstantEnvironment& environment, GrowthContext& context) {
    StemGraph& graph = plant.graph;
    const std::size_t internodeCount = graph.internodeCount();
    const std::size_t budCount = graph.budCount();
    const std::uint32_t step = plant.ageSteps + 1;

    if (internodeCount == 0 || internodeCount >= parameters.maxInternodes) {
        plant.ageSteps = step;
        return;
    }

    // Phase 1 — sample environment. Constant backend: every live bud collects
    // the same light. CSR grouping of buds by internode is rebuilt here and
    // reused by the allocation phase.
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

    // Phase 2 — accumulate energy rootward. Children always have higher
    // indices than parents, so one descending pass suffices.
    auto& energy = context.energy_;
    energy.assign(internodeCount, 0.0f);
    for (std::size_t i = 0; i < internodeCount; ++i) {
        const float budCountHere =
            static_cast<float>(budOffsets[i + 1] - budOffsets[i]);
        energy[i] = budCountHere * environment.light;
    }
    for (std::size_t i = internodeCount; i-- > 0;) {
        const Index parent = graph.internode(static_cast<Index>(i)).parent;
        if (parent != kInvalidIndex) {
            energy[parent] += energy[i];
        }
    }

    // Phase 3 — allocate resource tipward, split at each internode across
    // outlets (child internodes and attached buds) by bias-weighted energy.
    auto& resource = context.resource_;
    auto& budResource = context.budResource_;
    resource.assign(internodeCount, 0.0f);
    budResource.assign(budCount, 0.0f);
    for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
        if (graph.internode(i).parent == kInvalidIndex) {
            resource[i] = parameters.vigorScale * energy[i];
        }
    }
    for (Index i = 0; i < static_cast<Index>(internodeCount); ++i) {
        const float available = resource[i];
        if (available <= 0.0f) {
            continue;
        }
        const Internode& node = graph.internode(i);

        float weightSum = 0.0f;
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const bool continues = graph.internode(child).branchOrder == node.branchOrder;
            weightSum += outletBias(parameters, continues) * energy[child];
        }
        for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
            const Bud& bud = graph.bud(budIndices[slot]);
            weightSum += outletBias(parameters, bud.type == BudType::Apical) * environment.light;
        }
        if (weightSum <= 0.0f) {
            continue;
        }

        const float scale = available / weightSum;
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const bool continues = graph.internode(child).branchOrder == node.branchOrder;
            resource[child] += scale * outletBias(parameters, continues) * energy[child];
        }
        for (std::uint32_t slot = budOffsets[i]; slot < budOffsets[i + 1]; ++slot) {
            const Index budIndex = budIndices[slot];
            const Bud& bud = graph.bud(budIndex);
            budResource[budIndex] =
                scale * outletBias(parameters, bud.type == BudType::Apical) * environment.light;
        }
    }

    // Phase 4 — bud fate: dormant buds beyond the dormancy limit die.
    for (Index budIndex = 0; budIndex < static_cast<Index>(budCount); ++budIndex) {
        Bud& bud = graph.bud(budIndex);
        if (bud.state == BudState::Dormant && step - bud.creationStep > parameters.budDormancyLimit) {
            bud.state = BudState::Dead;
        }
    }

    // Phase 5 — shoot growth. Only buds that existed at step start grow;
    // additions during the loop are next step's population.
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
        growShoot(plant, parameters, budIndex, metamerCount, step);
    }

    // Phase 9 — aging.
    plant.ageSteps = step;
}

void grow(Plant& plant, const GrowthParameters& parameters, const ConstantEnvironment& environment,
          std::uint32_t steps, GrowthContext& context) {
    for (std::uint32_t i = 0; i < steps; ++i) {
        growStep(plant, parameters, environment, context);
    }
}

} // namespace flora

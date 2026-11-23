#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "flora/growth/simulation.hpp"

using namespace flora;

namespace {

Plant grownPlant(std::uint64_t seed, std::uint32_t steps, const GrowthParameters& parameters,
                 const ConstantEnvironment& environment = {}) {
    Plant plant;
    plant.seed = seed;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, environment, steps, context);
    return plant;
}

float meanDirectionComponent(const StemGraph& graph, float Vec3::* component) {
    float sum = 0.0f;
    std::size_t counted = 0;
    for (const Internode& node : graph.internodes()) {
        if (node.alive && node.branchOrder > 0) { // laterals show tropism; trunk starts vertical
            sum += node.direction.*component;
            ++counted;
        }
    }
    return counted > 0 ? sum / static_cast<float>(counted) : 0.0f;
}

GrowthParameters steeringOnly() {
    GrowthParameters parameters;
    parameters.gravitropism = 0.0f;
    parameters.phototropism = 0.0f;
    parameters.lateralGravitropism = 0.0f;
    parameters.lateralPhototropism = 0.0f;
    parameters.gnarl = 0.0f;
    parameters.branchAngleJitter = 0.0f;
    parameters.dropWeakBranchRatio = 0.0f; // isolate orientation effects
    return parameters;
}

} // namespace

TEST_CASE("gravitropism turns lateral growth upward") {
    GrowthParameters neutral = steeringOnly();
    GrowthParameters rising = neutral;
    rising.gravitropism = 0.25f;
    rising.lateralGravitropism = 0.25f; // branches are steered separately

    const Plant flat = grownPlant(21, 14, neutral);
    const Plant lifted = grownPlant(21, 14, rising);
    CHECK(meanDirectionComponent(lifted.graph, &Vec3::z) >
          meanDirectionComponent(flat.graph, &Vec3::z) + 0.1f);
}

TEST_CASE("negative gravitropism droops") {
    GrowthParameters neutral = steeringOnly();
    GrowthParameters weeping = neutral;
    weeping.gravitropism = -0.2f;
    weeping.lateralGravitropism = -0.2f;

    const Plant flat = grownPlant(21, 14, neutral);
    const Plant drooped = grownPlant(21, 14, weeping);
    CHECK(meanDirectionComponent(drooped.graph, &Vec3::z) <
          meanDirectionComponent(flat.graph, &Vec3::z) - 0.1f);
}

TEST_CASE("branches steer toward the gravitropic set-point angle") {
    const auto meanAngleFromVertical = [](const StemGraph& graph) {
        float sum = 0.0f;
        std::size_t counted = 0;
        for (const Internode& node : graph.internodes()) {
            if (node.alive && node.branchOrder > 0) {
                sum += std::acos(std::clamp(node.direction.z, -1.0f, 1.0f));
                ++counted;
            }
        }
        return counted > 0 ? sum / static_cast<float>(counted) : 0.0f;
    };

    GrowthParameters raised = steeringOnly();
    raised.lateralSetPointAngle = 0.5236f; // 30 deg from vertical
    raised.lateralSetPointStrength = 0.4f;
    GrowthParameters lowered = raised;
    lowered.lateralSetPointAngle = 2.0944f; // 120 deg: below horizontal

    const Plant upswept = grownPlant(24, 14, raised);
    const Plant weeping = grownPlant(24, 14, lowered);

    CHECK(meanAngleFromVertical(upswept.graph) < 1.0f);  // pulled up toward 30 deg
    CHECK(meanAngleFromVertical(weeping.graph) > 1.6f);  // pushed past horizontal
    CHECK(meanDirectionComponent(upswept.graph, &Vec3::z) >
          meanDirectionComponent(weeping.graph, &Vec3::z) + 0.3f);
}

TEST_CASE("the set point holds branch inclination against gnarl drift") {
    // The point of a set point over a plain ±Z pull: it is restoring. Gnarl and
    // stacked branch angles are a random walk over inclination, so without a
    // set point deep orders end up pointing anywhere, half of them at the
    // ground.
    const float setPoint = 1.0472f; // 60 deg from vertical
    const auto meanDeviationFrom = [setPoint](const StemGraph& graph) {
        float sum = 0.0f;
        std::size_t counted = 0;
        for (const Internode& node : graph.internodes()) {
            if (node.alive && node.branchOrder > 0) {
                sum += std::fabs(std::acos(std::clamp(node.direction.z, -1.0f, 1.0f)) - setPoint);
                ++counted;
            }
        }
        return counted > 0 ? sum / static_cast<float>(counted) : 0.0f;
    };

    GrowthParameters drifting = steeringOnly();
    drifting.gnarl = 0.2f;
    drifting.branchAngle = 1.5708f;
    GrowthParameters holding = drifting;
    holding.lateralSetPointAngle = setPoint;
    holding.lateralSetPointStrength = 0.3f;

    const Plant wandered = grownPlant(25, 16, drifting);
    const Plant held = grownPlant(25, 16, holding);
    CHECK(meanDeviationFrom(held.graph) < meanDeviationFrom(wandered.graph) * 0.6f);
    CHECK(meanDirectionComponent(held.graph, &Vec3::z) >
          meanDirectionComponent(wandered.graph, &Vec3::z) + 0.1f);
}

// Mean inclination from vertical of first-order branch wood, bucketed by the
// relative crown depth of the axis it belongs to. Mirrors the simulation's own
// crown frame: 0 at the leader tip, 1 at the lowest live branch attachment.
std::vector<float> meanInclinationByCrownZone(const StemGraph& graph,
                                              const std::vector<float>& zoneEnds) {
    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);

    float top = 0.0f;
    float lowestBranch = 0.0f;
    bool anyBranch = false;
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive) {
            continue;
        }
        top = std::max(top, tips[i].z);
        if (node.branchOrder == 0) {
            continue;
        }
        lowestBranch = anyBranch ? std::min(lowestBranch, tips[i].z) : tips[i].z;
        anyBranch = true;
    }
    const float depth = anyBranch ? std::max(0.001f, top - lowestBranch) : 0.001f;

    const auto originHeight = [&](Index index) {
        Index node = index;
        while (true) {
            const Internode& current = graph.internode(node);
            if (current.parent == kInvalidIndex) {
                return 0.0f;
            }
            if (graph.internode(current.parent).branchOrder != current.branchOrder) {
                return tips[current.parent].z;
            }
            node = current.parent;
        }
    };

    std::vector<float> sums(zoneEnds.size(), 0.0f);
    std::vector<std::size_t> counts(zoneEnds.size(), 0);
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.branchOrder != 1) {
            continue;
        }
        const float relative = std::clamp((top - originHeight(i)) / depth, 0.0f, 1.0f);
        for (std::size_t zone = 0; zone < zoneEnds.size(); ++zone) {
            if (relative <= zoneEnds[zone]) {
                sums[zone] += std::acos(std::clamp(node.direction.z, -1.0f, 1.0f));
                ++counts[zone];
                break;
            }
        }
    }
    std::vector<float> means(zoneEnds.size(), 0.0f);
    for (std::size_t zone = 0; zone < zoneEnds.size(); ++zone) {
        means[zone] = counts[zone] > 0 ? sums[zone] / static_cast<float>(counts[zone]) : 0.0f;
    }
    return means;
}

GrowthParameters gradedCrown() {
    GrowthParameters parameters = steeringOnly();
    parameters.branchAngle = 1.3614f;              // 78 deg emergence
    parameters.lateralSetPointStrength = 0.45f;
    parameters.lateralSetPointAngleApex = 0.5585f; // 32 deg: acute at the leader
    parameters.lateralSetPointAngle = 1.5010f;     // 86 deg: horizontal mid-canopy
    parameters.lateralSetPointAngleBase = 1.8500f; // 106 deg: drooping at the base
    parameters.crownApexFraction = 0.13f;
    parameters.crownMidFraction = 0.6f;
    return parameters;
}

TEST_CASE("the set-point gradient inclines branches by crown position") {
    const std::vector<float> zones{0.13f, 0.6f, 1.0f};
    const Plant graded = grownPlant(42, 24, gradedCrown());
    const std::vector<float> means = meanInclinationByCrownZone(graded.graph, zones);

    MESSAGE("apex " << means[0] * 57.2958f << " deg, mid " << means[1] * 57.2958f << " deg, lower "
                    << means[2] * 57.2958f << " deg");

    // Acute at the apex, horizontal in the mid canopy, drooping below it.
    CHECK(means[0] < means[1]);
    CHECK(means[1] < means[2]);
    CHECK(means[0] < 1.2f);  // apex clearly above horizontal
    CHECK(means[2] > 1.45f); // lower crown at or past horizontal
}

TEST_CASE("reorientation turns wood the whorl grew before it descended") {
    // A bough emerges at the apex angle and its wood is frozen there, so
    // without reorientation the lower crown keeps the angle it was born with
    // however far the crown has grown past it.
    const std::vector<float> zones{0.13f, 0.6f, 1.0f};
    GrowthParameters frozen = gradedCrown();
    GrowthParameters reorienting = frozen;
    reorienting.setPointReorientRate = 0.34f;

    // Reorienting also moves positions, so it changes light and hence which
    // branches survive: comparing raw means compares different populations.
    // The causal claim is that wood ends up closer to the angle its zone asks
    // for, which is population-independent.
    const auto deviationFromBase = [&](const Plant& plant) {
        const std::vector<float> means = meanInclinationByCrownZone(plant.graph, zones);
        return std::fabs(means[2] - reorienting.lateralSetPointAngleBase);
    };

    const float frozenDeviation = deviationFromBase(grownPlant(42, 24, frozen));
    const float turnedDeviation = deviationFromBase(grownPlant(42, 24, reorienting));

    CHECK(turnedDeviation < frozenDeviation);
    const std::vector<float> turnedMeans =
        meanInclinationByCrownZone(grownPlant(42, 24, reorienting).graph, zones);
    CHECK(turnedMeans[0] < turnedMeans[2]); // apex still the acute end
}

TEST_CASE("a flat gradient reproduces the single set-point angle") {
    // Schema v9 defaults must collapse to v8 behaviour: apex and base equal to
    // the mid-canopy angle leave no crown-position dependence.
    const std::vector<float> zones{0.13f, 0.6f, 1.0f};
    GrowthParameters flat = gradedCrown();
    flat.lateralSetPointAngleApex = flat.lateralSetPointAngle;
    flat.lateralSetPointAngleBase = flat.lateralSetPointAngle;

    const Plant plant = grownPlant(42, 24, flat);
    const std::vector<float> means = meanInclinationByCrownZone(plant.graph, zones);
    CHECK(std::fabs(means[0] - means[2]) < 0.15f);
    CHECK(std::fabs(means[1] - means[2]) < 0.15f);
}

// Length-weighted inclination from vertical of branchlet wood (order 2 and up),
// how many distinct branchlet axes exist, and how many of those hang from a
// bearing axis shorter than the bare inner length asks for.
struct BranchletStats {
    float meanInclination = 0.0f;
    std::size_t axisCount = 0;
    std::size_t axesInsideOnset = 0;
};

BranchletStats branchletStats(const StemGraph& graph, float onsetDistance) {
    // Axis path from each internode back to its own axis origin, root to tip.
    std::vector<float> axisDistance(graph.internodeCount(), 0.0f);
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        const bool startsAxis = node.parent == kInvalidIndex ||
                                graph.internode(node.parent).branchOrder != node.branchOrder;
        axisDistance[i] = (startsAxis ? 0.0f : axisDistance[node.parent]) + node.length;
    }

    BranchletStats stats;
    double weighted = 0.0;
    double totalLength = 0.0;
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.branchOrder < 2 || node.parent == kInvalidIndex) {
            continue;
        }
        const double inclination = std::acos(std::clamp(node.direction.z, -1.0f, 1.0f));
        weighted += inclination * static_cast<double>(node.length);
        totalLength += static_cast<double>(node.length);
        if (graph.internode(node.parent).branchOrder == node.branchOrder) {
            continue;
        }
        ++stats.axisCount;
        stats.axesInsideOnset += axisDistance[node.parent] < onsetDistance ? 1 : 0;
    }
    stats.meanInclination =
        totalLength > 0.0 ? static_cast<float>(weighted / totalLength) : 0.0f;
    return stats;
}

GrowthParameters pendulousCrown() {
    GrowthParameters parameters = gradedCrown();
    parameters.setPointReorientRate = 0.34f;
    parameters.secondaryDroopFactor = 0.6f;
    return parameters;
}

TEST_CASE("branchlets hang below the boughs that bear them") {
    GrowthParameters upright = pendulousCrown();
    upright.secondaryDroopFactor = 0.0f;

    const BranchletStats hanging = branchletStats(grownPlant(42, 24, pendulousCrown()).graph, 0.0f);
    const BranchletStats flat = branchletStats(grownPlant(42, 24, upright).graph, 0.0f);
    REQUIRE(hanging.axisCount > 0);
    REQUIRE(flat.axisCount > 0);

    MESSAGE("branchlets hang at " << hanging.meanInclination * 57.2958f << " deg, undrooped "
                                  << flat.meanInclination * 57.2958f << " deg");

    // Past horizontal and well past the primary set point they would otherwise
    // hold: a curtain, not a set of small boughs.
    CHECK(hanging.meanInclination > 2.0944f); // 120 deg
    CHECK(hanging.meanInclination > flat.meanInclination + 0.3f);
}

TEST_CASE("droop leaves the primary boughs on the crown gradient") {
    // Branchlets hang; the boughs bearing them must keep the schema v9 profile.
    const std::vector<float> zones{0.13f, 0.6f, 1.0f};
    GrowthParameters upright = pendulousCrown();
    upright.secondaryDroopFactor = 0.0f;

    const std::vector<float> hanging =
        meanInclinationByCrownZone(grownPlant(42, 24, pendulousCrown()).graph, zones);
    const std::vector<float> flat =
        meanInclinationByCrownZone(grownPlant(42, 24, upright).graph, zones);

    CHECK(hanging[0] < hanging[1]);
    CHECK(hanging[1] < hanging[2]);
    CHECK(std::fabs(hanging[0] - flat[0]) < 0.15f);
}

TEST_CASE("the bare inner length keeps branchlets off young boughs") {
    // Buds are laid down at the axis tip, so the gate is an age gate: nothing
    // may hang from a bough that has not yet extended past it.
    GrowthParameters gated = pendulousCrown();
    gated.secondaryOnsetDistance = 0.4f;

    const BranchletStats ungated = branchletStats(grownPlant(42, 24, pendulousCrown()).graph, 0.4f);
    const BranchletStats stats = branchletStats(grownPlant(42, 24, gated).graph, 0.4f);

    REQUIRE(ungated.axesInsideOnset > 0); // the gate has something to remove
    CHECK(stats.axesInsideOnset == 0);
    CHECK(stats.axisCount < ungated.axisCount);
}

TEST_CASE("branchlet density gates the curtain, not the whorls") {
    // Thinning changes shading and competition, so surviving branchlet counts
    // are not monotone in the density. What the gate does claim is where it
    // applies: only bud slots on order-1-and-up shoots, never the leader's
    // whorls.
    GrowthParameters bare = pendulousCrown();
    bare.lateralBudsPerMetamer = 4;
    GrowthParameters full = bare;
    bare.secondaryBranchDensity = 0.0f;

    const Plant barePlant = grownPlant(42, 18, bare);
    const Plant fullPlant = grownPlant(42, 18, full);

    const auto boughAxes = [](const StemGraph& graph) {
        std::size_t count = 0;
        for (const Internode& node : graph.internodes()) {
            count += node.alive && node.branchOrder == 1 && node.parent != kInvalidIndex &&
                             graph.internode(node.parent).branchOrder == 0
                         ? 1
                         : 0;
        }
        return count;
    };

    CHECK(branchletStats(barePlant.graph, 0.0f).axisCount == 0);
    CHECK(branchletStats(fullPlant.graph, 0.0f).axisCount > 0);
    CHECK(boughAxes(barePlant.graph) > 0);
}

TEST_CASE("zero set-point strength leaves lateral steering to gravitropism") {
    GrowthParameters plain = steeringOnly();
    plain.lateralGravitropism = 0.2f;
    GrowthParameters withUnusedSetPoint = plain;
    withUnusedSetPoint.lateralSetPointAngle = 2.6f; // would drive branches down
    withUnusedSetPoint.lateralSetPointStrength = 0.0f;

    const Plant reference = grownPlant(26, 12, plain);
    const Plant unchanged = grownPlant(26, 12, withUnusedSetPoint);
    REQUIRE(reference.graph.internodeCount() == unchanged.graph.internodeCount());
    for (Index i = 0; i < static_cast<Index>(reference.graph.internodeCount()); ++i) {
        CHECK(reference.graph.internode(i).direction.z ==
              doctest::Approx(unchanged.graph.internode(i).direction.z));
    }
}

TEST_CASE("phototropism bends growth toward the light direction") {
    // Shading off: the raycast backend replaces the environment's light
    // direction with the direction of the clearest sky, which is what is
    // under test here.
    GrowthParameters parameters = steeringOnly();
    parameters.phototropism = 0.3f;
    parameters.shadeModel = ShadeModel::Off;
    GrowthParameters unlit = steeringOnly();
    unlit.shadeModel = ShadeModel::Off;

    ConstantEnvironment sideLight;
    sideLight.lightDirection = Vec3{1.0f, 0.0f, 0.3f}.normalizedOr(kUpAxis);

    const Plant straight = grownPlant(22, 14, unlit);
    const Plant bent = grownPlant(22, 14, parameters, sideLight);
    CHECK(meanDirectionComponent(bent.graph, &Vec3::x) >
          meanDirectionComponent(straight.graph, &Vec3::x) + 0.1f);
}

TEST_CASE("gnarl perturbs continuation directions") {
    GrowthParameters straight = steeringOnly();
    GrowthParameters gnarled = straight;
    gnarled.gnarl = 0.15f;

    const Plant regular = grownPlant(23, 10, straight);
    const Plant twisted = grownPlant(23, 10, gnarled);

    // Without gnarl every continuation keeps its parent's direction exactly;
    // with gnarl they deviate.
    const auto continuationDeviation = [](const StemGraph& graph) {
        float deviation = 0.0f;
        for (const Internode& node : graph.internodes()) {
            if (node.parent != kInvalidIndex &&
                node.branchOrder == graph.internode(node.parent).branchOrder) {
                deviation += 1.0f - node.direction.dot(graph.internode(node.parent).direction);
            }
        }
        return deviation;
    };
    CHECK(std::fabs(continuationDeviation(regular.graph)) < 1.0e-3f); // renormalization noise only
    CHECK(continuationDeviation(twisted.graph) > 0.01f);
}

TEST_CASE("secondary thickening grows the trunk and preserves taper") {
    GrowthParameters parameters;
    parameters.dropWeakBranchRatio = 0.0f;
    parameters.shadeModel = ShadeModel::Off; // isolate thickening from light competition
    const Plant plant = grownPlant(31, 11, parameters); // below the internode cap

    const float rootRadius = plant.graph.internode(0).radius;
    CHECK(rootRadius > parameters.baseGrowthThickness * 3.0f);

    // The newest internodes still have their initial radius: taper exists.
    float newestRadius = 0.0f;
    for (const Internode& node : plant.graph.internodes()) {
        if (node.creationStep == plant.ageSteps) {
            newestRadius = node.radius;
            break;
        }
    }
    CHECK(newestRadius == doctest::Approx(parameters.baseGrowthThickness));
    CHECK(rootRadius > newestRadius * 3.0f);
}

TEST_CASE("radii never shrink during growth") {
    GrowthParameters parameters;
    Plant plant;
    plant.seed = 33;
    plantSeedling(plant, parameters);
    GrowthContext context;
    float previousRootRadius = plant.graph.internode(0).radius;
    for (int step = 0; step < 18; ++step) {
        growStep(plant, parameters, ConstantEnvironment{}, context);
        const float rootRadius = plant.graph.internode(0).radius;
        CHECK(rootRadius >= previousRootRadius);
        previousRootRadius = rootRadius;
    }
}

TEST_CASE("cost pressure sheds branches and keeps invariants") {
    GrowthParameters keepAll;
    keepAll.dropWeakBranchRatio = 0.0f;
    keepAll.shadeModel = ShadeModel::Off; // isolate shedding from light competition
    GrowthParameters shedding = keepAll;
    shedding.dropWeakBranchRatio = 1.0f;
    shedding.maintenanceCostPerVolume = 1.0e5f; // strong cost pressure

    const Plant retained = grownPlant(35, 11, keepAll); // below the internode cap
    const Plant pruned = grownPlant(35, 11, shedding);

    CHECK(pruned.graph.internodeCount() < retained.graph.internodeCount());
    std::string errors;
    CHECK_MESSAGE(pruned.graph.validate(&errors), errors);
}

TEST_CASE("shedding is rate-limited per step") {
    GrowthParameters parameters;
    parameters.dropWeakBranchRatio = 5.0f; // everything looks non-viable
    parameters.maintenanceCostPerVolume = 1.0e6f;
    parameters.maxShedFractionPerStep = 0.05f;
    parameters.branchDecaySteps = 0; // keep deadwood so counts are observable

    Plant plant;
    plant.seed = 36;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{}, 12, context);

    // Under total cost pressure the tree still keeps a live majority each
    // step; mass die-off in one step is exactly what the limiter prevents.
    const std::size_t total = plant.graph.internodeCount();
    const std::size_t dead = plant.graph.deadInternodeCount();
    CHECK(dead > 0);
    CHECK(dead < total);
}

TEST_CASE("deadwood decays after the configured delay") {
    StemGraph graph;
    const Index root = graph.addRoot(kUpAxis, 1.0f, 0.1f, 0);
    const Index branch = graph.addInternode(root, AxisRelation::Lateral, Vec3{1, 0, 0}, 0.5f, 0.02f, 1);
    graph.addInternode(branch, AxisRelation::Continuation, Vec3{1, 0, 0}, 0.5f, 0.02f, 2);

    graph.markSubtreeDead(branch, 5);
    graph.sweepDeadBuds();
    REQUIRE(graph.internodeCount() == 3);

    graph.removeDecayed(7, 4); // age 2 of 4: still standing
    CHECK(graph.internodeCount() == 3);
    CHECK(graph.deadInternodeCount() == 2);

    graph.removeDecayed(9, 4); // age 4: gone
    CHECK(graph.internodeCount() == 1);
    CHECK(graph.deadInternodeCount() == 0);

    std::string errors;
    CHECK_MESSAGE(graph.validate(&errors), errors);
}

TEST_CASE("bending sags a loaded limb and leaves an unloaded one straight") {
    GrowthParameters parameters = steeringOnly();
    parameters.branchStiffness = 1.0e4f;
    parameters.bendIntegration = 0.5f;

    // A horizontal cantilever carrying wood, versus the same stem bare.
    StemGraph loaded;
    Index tip = loaded.addRoot(Vec3{1.0f, 0.0f, 0.0f}, 1.0f, 0.05f, 0);
    for (int i = 0; i < 6; ++i) {
        tip = loaded.addInternode(tip, AxisRelation::Continuation, Vec3{1.0f, 0.0f, 0.0f}, 1.0f,
                                  0.05f, 0);
    }
    StemGraph bare;
    bare.addRoot(Vec3{1.0f, 0.0f, 0.0f}, 1.0f, 0.05f, 0);

    Plant loadedPlant;
    loadedPlant.graph = loaded;
    Plant barePlant;
    barePlant.graph = bare;
    GrowthContext context;
    growStep(loadedPlant, parameters, ConstantEnvironment{0.0f}, context);
    growStep(barePlant, parameters, ConstantEnvironment{0.0f}, context);

    // Both sag under their own weight; the one carrying six more internodes
    // sags far more.
    CHECK(loadedPlant.graph.internode(0).direction.z < -0.01f);
    CHECK(loadedPlant.graph.internode(0).direction.z < barePlant.graph.internode(0).direction.z);
}

TEST_CASE("stiffer wood and weaker integration both bend less") {
    auto sagOf = [](float stiffness, float integration) {
        GrowthParameters parameters = steeringOnly();
        parameters.branchStiffness = stiffness;
        parameters.bendIntegration = integration;
        StemGraph graph;
        Index tip = graph.addRoot(Vec3{1.0f, 0.0f, 0.0f}, 1.0f, 0.05f, 0);
        for (int i = 0; i < 6; ++i) {
            tip = graph.addInternode(tip, AxisRelation::Continuation, Vec3{1.0f, 0.0f, 0.0f}, 1.0f,
                                     0.05f, 0);
        }
        Plant plant;
        plant.graph = graph;
        GrowthContext context;
        growStep(plant, parameters, ConstantEnvironment{0.0f}, context);
        return -plant.graph.internode(0).direction.z;
    };
    // Stiffnesses chosen below the per-step bend cap so the comparison is not
    // between two clamped values.
    CHECK(sagOf(1.0e6f, 0.5f) > sagOf(1.0e7f, 0.5f));
    CHECK(sagOf(1.0e6f, 0.5f) > sagOf(1.0e6f, 0.25f));
    CHECK(sagOf(1.0e6f, 0.0f) == doctest::Approx(0.0f)); // disabled
}

TEST_CASE("bending accumulates over steps and stays bounded") {
    GrowthParameters parameters = steeringOnly();
    parameters.branchStiffness = 1.0e4f;
    parameters.bendIntegration = 0.5f;
    StemGraph graph;
    Index tip = graph.addRoot(Vec3{1.0f, 0.0f, 0.0f}, 1.0f, 0.05f, 0);
    for (int i = 0; i < 6; ++i) {
        tip = graph.addInternode(tip, AxisRelation::Continuation, Vec3{1.0f, 0.0f, 0.0f}, 1.0f,
                                 0.05f, 0);
    }
    Plant plant;
    plant.graph = graph;
    GrowthContext context;
    float previous = 0.0f;
    for (int step = 0; step < 12; ++step) {
        growStep(plant, parameters, ConstantEnvironment{0.0f}, context);
        const float sag = -plant.graph.internode(0).direction.z;
        CHECK(sag >= previous - 1.0e-5f); // permanent: never springs back
        CHECK(plant.graph.internode(0).direction.isFinite());
        previous = sag;
    }
    CHECK(previous <= 1.0f);
    CHECK(plant.graph.validate());
}

TEST_CASE("apical-weighted thickening keeps suppressed laterals slimmer") {
    auto lateralShare = [](float apicalWeight) {
        GrowthParameters parameters;
        parameters.thicknessApicalWeight = apicalWeight;
        parameters.apicalControl = 0.85f; // leader takes most of the resource
        Plant plant = grownPlant(4, 14, parameters);
        float trunk = 0.0f;
        float laterals = 0.0f;
        for (const Internode& node : plant.graph.internodes()) {
            if (!node.alive) {
                continue;
            }
            (node.branchOrder == 0 ? trunk : laterals) += node.radius;
        }
        return trunk > 0.0f ? laterals / trunk : 0.0f;
    };
    CHECK(lateralShare(1.0f) < lateralShare(0.0f));
}

namespace {

// Crown base: where the lowest surviving primary bough leaves the trunk. The
// attachment, not the bough's own lowest wood -- a drooping limb reaches well
// below the point it grew from, and can reach below the ground plane.
float lowestBoughAttachment(const StemGraph& graph) {
    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);
    float lowest = 0.0f;
    bool any = false;
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.alive || node.branchOrder != 1 || node.parent == kInvalidIndex ||
            graph.internode(node.parent).branchOrder != 0) {
            continue;
        }
        const float attachment = tips[node.parent].z;
        lowest = any ? std::min(lowest, attachment) : attachment;
        any = true;
    }
    return any ? lowest : 0.0f;
}

// Trunk internodes that bear at least one snag.
std::vector<Index> snagBearingWhorls(const StemGraph& graph) {
    std::vector<Index> whorls;
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        if (!node.stub || node.parent == kInvalidIndex ||
            graph.internode(node.parent).branchOrder != 0) {
            continue;
        }
        if (std::find(whorls.begin(), whorls.end(), node.parent) == whorls.end()) {
            whorls.push_back(node.parent);
        }
    }
    return whorls;
}

GrowthParameters unlitCrown() {
    GrowthParameters parameters;
    parameters.shadeModel = ShadeModel::Off;   // no light gradient to confound height
    parameters.dropShadedGrowthLight = 0.0f;   // and no shade shedding at all
    return parameters;
}

} // namespace

TEST_CASE("crown-base clearance lifts the crown base without any light rule") {
    GrowthParameters keepAll = unlitCrown();
    GrowthParameters clearing = keepAll;
    clearing.crownBaseHeightFraction = 0.25f;
    clearing.deadStubLength = 0.2f;

    const Plant full = grownPlant(51, 18, keepAll);
    const Plant cleared = grownPlant(51, 18, clearing);

    CHECK(lowestBoughAttachment(cleared.graph) > lowestBoughAttachment(full.graph));

    // Clearance is about the bole, so the bole survives it.
    for (const Internode& node : cleared.graph.internodes()) {
        if (node.branchOrder == 0) {
            CHECK(node.alive);
        }
    }
    std::string errors;
    CHECK_MESSAGE(cleared.graph.validate(&errors), errors);
}

TEST_CASE("clearance leaves snags on the bole") {
    GrowthParameters clearing = unlitCrown();
    clearing.crownBaseHeightFraction = 0.25f;
    clearing.deadStubLength = 0.2f;

    const Plant cleared = grownPlant(51, 18, clearing);
    const std::vector<Index> whorls = snagBearingWhorls(cleared.graph);
    REQUIRE_FALSE(whorls.empty());

    std::vector<Vec3> tips;
    cleared.graph.computeTipPositions(tips);
    const float crownBase = lowestBoughAttachment(cleared.graph);
    for (const Internode& node : cleared.graph.internodes()) {
        if (!node.stub) {
            continue;
        }
        CHECK_FALSE(node.alive);
        CHECK(tips[node.parent].z < crownBase); // snags sit under the live crown
    }
}

TEST_CASE("snags carry no foliage") {
    GrowthParameters clearing = unlitCrown();
    clearing.crownBaseHeightFraction = 0.25f;
    clearing.deadStubLength = 0.2f;

    const Plant cleared = grownPlant(51, 18, clearing);
    std::vector<Vec3> tips;
    cleared.graph.computeTipPositions(tips);
    std::vector<FoliageCluster> clusters;
    computeFoliagePlacement(cleared.graph, clearing, tips, clusters);
    REQUIRE_FALSE(clusters.empty());
    for (const FoliageCluster& cluster : clusters) {
        CHECK_FALSE(cleared.graph.internode(cluster.internode).stub);
    }
}

TEST_CASE("clearance takes at most one whorl per step") {
    GrowthParameters growing = unlitCrown();
    Plant plant;
    plant.seed = 52;
    plantSeedling(plant, growing);
    GrowthContext context;
    grow(plant, growing, ConstantEnvironment{}, 16, context);
    REQUIRE(snagBearingWhorls(plant.graph).empty());

    GrowthParameters clearing = growing;
    clearing.crownBaseHeightFraction = 0.95f; // nearly every whorl is under the line
    clearing.deadStubLength = 0.2f;
    clearing.dropWeakBranchRatio = 0.0f;      // isolate clearance from cost shedding
    clearing.branchDecaySteps = 0;            // keep the deadwood observable
    growStep(plant, clearing, ConstantEnvironment{}, context);

    CHECK(snagBearingWhorls(plant.graph).size() == 1);
}

TEST_CASE("a zero clearance fraction reproduces the unpruned tree") {
    GrowthParameters plain = unlitCrown();
    GrowthParameters configured = plain;
    configured.deadStubLength = 0.25f; // set, but nothing may act on it

    const Plant reference = grownPlant(53, 20, plain);
    const Plant candidate = grownPlant(53, 20, configured);

    REQUIRE(candidate.graph.internodeCount() == reference.graph.internodeCount());
    std::vector<Vec3> referenceTips;
    std::vector<Vec3> candidateTips;
    reference.graph.computeTipPositions(referenceTips);
    candidate.graph.computeTipPositions(candidateTips);
    for (std::size_t i = 0; i < referenceTips.size(); ++i) {
        CHECK(candidateTips[i].z == doctest::Approx(referenceTips[i].z));
    }
    for (const Internode& node : candidate.graph.internodes()) {
        CHECK_FALSE(node.stub);
    }
}

TEST_CASE("an establishing lateral is not shed for being unprofitable") {
    // Cost pressure high enough that no young branch could ever pay for itself.
    GrowthParameters harsh = unlitCrown();
    harsh.dropWeakBranchRatio = 5.0f;
    harsh.maintenanceCostPerVolume = 1.0e6f;
    harsh.branchDecaySteps = 0; // keep the dead wood countable

    GrowthParameters sheltered = harsh;
    sheltered.branchEstablishSteps = 4;

    const Plant exposed = grownPlant(54, 12, harsh);
    const Plant spared = grownPlant(54, 12, sheltered);

    // Deadwood is retained, so live wood is the total minus the casualties.
    // Absolute death counts say nothing here: the sheltered tree grows larger,
    // so it also has more wood left to lose later.
    const std::size_t exposedLive =
        exposed.graph.internodeCount() - exposed.graph.deadInternodeCount();
    const std::size_t sparedLive =
        spared.graph.internodeCount() - spared.graph.deadInternodeCount();
    CHECK(sparedLive > exposedLive);

    // No shade rule is active here, so the only thing that prunes is cost, and
    // a dead node whose parent still lives is exactly a prune root. Deeper
    // casualties died with the branch above them and carry no grace of their
    // own -- the exemption protects the branch that is tested, not everything
    // hanging off one that failed.
    for (Index i = 0; i < static_cast<Index>(spared.graph.internodeCount()); ++i) {
        const Internode& node = spared.graph.internode(i);
        if (node.alive || node.parent == kInvalidIndex ||
            !spared.graph.internode(node.parent).alive) {
            continue;
        }
        CHECK(node.deathStep - node.creationStep >= sheltered.branchEstablishSteps);
    }
    std::string errors;
    CHECK_MESSAGE(spared.graph.validate(&errors), errors);
}

TEST_CASE("the grace period keeps the whorl ladder unbroken") {
    // Share of leader metamers still carrying a live whorl. The gap itself is
    // not a stable measure -- the sheltered tree grows taller, so it has more
    // trunk to leave a gap in -- but coverage is scale free, and a broken
    // ladder is exactly a run of leader metamers with nothing left on them.
    auto whorlCoverage = [](const GrowthParameters& parameters) {
        const Plant plant = grownPlant(55, 26, parameters);
        const StemGraph& graph = plant.graph;
        std::size_t leaderMetamers = 0;
        std::size_t carrying = 0;
        for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
            const Internode& node = graph.internode(i);
            if (!node.alive || node.branchOrder != 0) {
                continue;
            }
            ++leaderMetamers;
            for (Index child = node.firstChild; child != kInvalidIndex;
                 child = graph.internode(child).nextSibling) {
                const Internode& bough = graph.internode(child);
                if (bough.alive && bough.branchOrder == 1) {
                    ++carrying;
                    break;
                }
            }
        }
        return leaderMetamers > 0 ? static_cast<float>(carrying) /
                                        static_cast<float>(leaderMetamers)
                                  : 0.0f;
    };

    // Both rules live, and self-shading on: the failure needs a real light
    // gradient, since it is the shade rule stripping a young whorl that leaves
    // the cost rule with nothing to weigh.
    GrowthParameters shedding;
    shedding.shadeModel = ShadeModel::Raycast;
    shedding.shadowIntensity = 6.0f;
    shedding.leafClusterSpacing = 0.12f;
    shedding.dropShadedGrowthLight = 0.25f;
    shedding.dropWeakBranchRatio = 1.0f;
    shedding.maintenanceCostPerVolume = 4.0e4f;
    GrowthParameters sheltered = shedding;
    sheltered.branchEstablishSteps = 3;

    const float exposed = whorlCoverage(shedding);
    const float spared = whorlCoverage(sheltered);
    MESSAGE("whorl coverage " << exposed << " exposed, " << spared << " sheltered");
    CHECK(spared > exposed);
}

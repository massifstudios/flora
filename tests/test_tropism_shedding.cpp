#include <doctest/doctest.h>

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

TEST_CASE("phototropism bends growth toward the light direction") {
    GrowthParameters parameters = steeringOnly();
    parameters.phototropism = 0.3f;

    ConstantEnvironment sideLight;
    sideLight.lightDirection = Vec3{1.0f, 0.0f, 0.3f}.normalizedOr(kUpAxis);

    const Plant straight = grownPlant(22, 14, steeringOnly());
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

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "flora/growth/simulation.hpp"

using namespace flora;

namespace {

Plant grownPlant(std::uint64_t seed, std::uint32_t steps, const GrowthParameters& parameters,
                 float light = 1.0f) {
    Plant plant;
    plant.seed = seed;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{light}, steps, context);
    return plant;
}

std::size_t countOrderZero(const StemGraph& graph) {
    std::size_t count = 0;
    for (const Internode& node : graph.internodes()) {
        if (node.branchOrder == 0) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("seedling: one internode, one apical bud") {
    Plant plant;
    plantSeedling(plant, GrowthParameters{});
    CHECK(plant.graph.internodeCount() == 1);
    CHECK(plant.graph.budCount() == 1);
    CHECK(plant.graph.bud(0).type == BudType::Apical);
}

TEST_CASE("growth is exactly reproducible for the same seed") {
    const GrowthParameters parameters;
    const Plant first = grownPlant(7, 18, parameters);
    const Plant second = grownPlant(7, 18, parameters);

    REQUIRE(first.graph.internodeCount() == second.graph.internodeCount());
    REQUIRE(first.graph.budCount() == second.graph.budCount());
    for (Index i = 0; i < static_cast<Index>(first.graph.internodeCount()); ++i) {
        const Internode& a = first.graph.internode(i);
        const Internode& b = second.graph.internode(i);
        CHECK(a.parent == b.parent);
        CHECK(a.direction == b.direction);
        CHECK(a.length == b.length);
        CHECK(a.radius == b.radius);
        CHECK(a.branchOrder == b.branchOrder);
        CHECK(a.creationStep == b.creationStep);
    }
    for (Index i = 0; i < static_cast<Index>(first.graph.budCount()); ++i) {
        const Bud& a = first.graph.bud(i);
        const Bud& b = second.graph.bud(i);
        CHECK(a.internode == b.internode);
        CHECK(a.direction == b.direction);
        CHECK(a.state == b.state);
        CHECK(a.rollAngle == b.rollAngle);
    }
}

TEST_CASE("different seeds diverge") {
    const GrowthParameters parameters;
    const Plant first = grownPlant(1, 18, parameters);
    const Plant second = grownPlant(2, 18, parameters);
    bool anyDifference = first.graph.internodeCount() != second.graph.internodeCount();
    if (!anyDifference) {
        for (Index i = 0; i < static_cast<Index>(first.graph.internodeCount()); ++i) {
            if (!(first.graph.internode(i).direction == second.graph.internode(i).direction)) {
                anyDifference = true;
                break;
            }
        }
    }
    CHECK(anyDifference);
}

TEST_CASE("structural invariants hold throughout growth") {
    const GrowthParameters parameters;
    Plant plant;
    plant.seed = 11;
    plantSeedling(plant, parameters);
    GrowthContext context;
    std::string errors;
    for (int step = 0; step < 22; ++step) {
        growStep(plant, parameters, ConstantEnvironment{}, context);
        REQUIRE_MESSAGE(plant.graph.validate(&errors), errors);
    }
    CHECK(plant.graph.internodeCount() > 100); // it actually grew
}

TEST_CASE("stronger apical control concentrates growth in the main axis") {
    GrowthParameters weak;
    weak.apicalControl = 0.55f;
    weak.branchAngleJitter = 0.0f;
    GrowthParameters strong = weak;
    strong.apicalControl = 0.85f;

    const Plant weakPlant = grownPlant(5, 16, weak);
    const Plant strongPlant = grownPlant(5, 16, strong);

    // Absolute axis length is resource-driven in both; the discriminating
    // effect of apical control is the share of structure in the leader.
    const auto fraction = [](const Plant& plant) {
        return static_cast<double>(countOrderZero(plant.graph)) /
               static_cast<double>(plant.graph.internodeCount());
    };
    CHECK(fraction(strongPlant) > fraction(weakPlant));
    CHECK(countOrderZero(strongPlant.graph) > 10); // the leader actually elongates
}

TEST_CASE("no light means no growth") {
    const Plant plant = grownPlant(3, 10, GrowthParameters{}, 0.0f);
    CHECK(plant.graph.internodeCount() == 1);
    CHECK(plant.ageSteps == 10);
}

TEST_CASE("unreachable activation threshold stalls growth") {
    GrowthParameters parameters;
    parameters.activationThreshold = 100.0f;
    const Plant plant = grownPlant(3, 10, parameters);
    CHECK(plant.graph.internodeCount() == 1);
}

TEST_CASE("internode cap is never exceeded") {
    GrowthParameters parameters;
    parameters.maxInternodes = 500;
    const Plant plant = grownPlant(9, 40, parameters);
    CHECK(plant.graph.internodeCount() <= 500);
    std::string errors;
    CHECK_MESSAGE(plant.graph.validate(&errors), errors);
}

TEST_CASE("whorl count controls lateral buds per metamer") {
    GrowthParameters parameters;
    parameters.lateralBudsPerMetamer = 2;
    Plant plant;
    plant.seed = 4;
    plantSeedling(plant, parameters);
    GrowthContext context;
    growStep(plant, parameters, ConstantEnvironment{}, context);

    const std::size_t newMetamers = plant.graph.internodeCount() - 1;
    CHECK(newMetamers >= 1);
    CHECK(plant.graph.budCount() == 1 + newMetamers * 2);
}

TEST_CASE("lateral buds emerge at the configured branch angle") {
    GrowthParameters parameters;
    parameters.branchAngleJitter = 0.0f;
    Plant plant;
    plant.seed = 4;
    plantSeedling(plant, parameters);
    GrowthContext context;
    growStep(plant, parameters, ConstantEnvironment{}, context);

    for (const Bud& bud : plant.graph.buds()) {
        if (bud.type != BudType::Lateral) {
            continue;
        }
        const Vec3 shootDirection = plant.graph.internode(bud.internode).direction;
        const float cosAngle = bud.direction.dot(shootDirection);
        CHECK(std::acos(cosAngle) == doctest::Approx(parameters.branchAngle).epsilon(0.001));
    }
}

TEST_CASE("successive lateral buds follow the phyllotactic spiral") {
    GrowthParameters parameters;
    parameters.branchAngleJitter = 0.0f;
    parameters.maxMetamersPerShoot = 3;
    parameters.vigorScale = 3.0f; // first shoot produces all three metamers
    Plant plant;
    plant.seed = 4;
    plantSeedling(plant, parameters);
    GrowthContext context;
    growStep(plant, parameters, ConstantEnvironment{}, context);

    // Buds on consecutive metamers of the first shoot differ in roll by the
    // golden angle.
    std::vector<const Bud*> laterals;
    for (const Bud& bud : plant.graph.buds()) {
        if (bud.type == BudType::Lateral) {
            laterals.push_back(&bud);
        }
    }
    REQUIRE(laterals.size() >= 2);
    for (std::size_t i = 1; i < laterals.size(); ++i) {
        const float delta = laterals[i]->rollAngle - laterals[i - 1]->rollAngle;
        CHECK(delta == doctest::Approx(parameters.phyllotaxisAngle).epsilon(0.001));
    }
}

TEST_CASE("regression: pinned structure for default parameters") {
    // Golden snapshot for (seed 42, 14 steps, defaults). A change here is a
    // deterministic-output change and requires a kSimulationRevision bump.
    const Plant plant = grownPlant(42, 14, GrowthParameters{});
    CHECK(plant.graph.internodeCount() == 12164);
    CHECK(plant.graph.budCount() == 12164);

    std::vector<Vec3> tips;
    plant.graph.computeTipPositions(tips);
    float height = 0.0f;
    for (const Vec3& tip : tips) {
        height = std::max(height, tip.z);
    }
    CHECK(height == doctest::Approx(12.950004f).epsilon(0.001));
}

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "flora/environment/environment.hpp"
#include "flora/environment/shadow_grid.hpp"
#include "flora/growth/simulation.hpp"

using namespace flora;

namespace {

Plant grownPlant(std::uint64_t seed, std::uint32_t steps, const GrowthParameters& parameters) {
    Plant plant;
    plant.seed = seed;
    plantSeedling(plant, parameters);
    GrowthContext context;
    grow(plant, parameters, ConstantEnvironment{}, steps, context);
    return plant;
}

// Fraction of live buds farther than half the crown radius from the trunk
// axis: high values mean foliage concentrated in an outer shell.
float outerShellBudFraction(const Plant& plant) {
    std::vector<Vec3> tips;
    plant.graph.computeTipPositions(tips);
    float crownRadius = 0.0f;
    for (const Bud& bud : plant.graph.buds()) {
        if (bud.state == BudState::Dead) {
            continue;
        }
        const Vec3& tip = tips[bud.internode];
        crownRadius = std::max(crownRadius, std::hypot(tip.x, tip.y));
    }
    if (crownRadius <= 0.0f) {
        return 0.0f;
    }
    std::size_t total = 0;
    std::size_t outer = 0;
    for (const Bud& bud : plant.graph.buds()) {
        if (bud.state == BudState::Dead) {
            continue;
        }
        const Vec3& tip = tips[bud.internode];
        ++total;
        if (std::hypot(tip.x, tip.y) > 0.5f * crownRadius) {
            ++outer;
        }
    }
    return total > 0 ? static_cast<float>(outer) / static_cast<float>(total) : 0.0f;
}

} // namespace

TEST_CASE("environment backends implement the light query interface") {
    ConstantEnvironment constant{0.75f, Vec3{0.0f, 1.0f, 0.0f}};
    const LightSample constantSample = constant.sampleLight(Vec3{10.0f, -3.0f, 25.0f});
    CHECK(constantSample.intensity == 0.75f);
    CHECK(constantSample.direction == Vec3{0.0f, 1.0f, 0.0f});

    SkyEnvironment sky;
    const LightSample skySample = sky.sampleLight(Vec3{});
    CHECK(skySample.intensity == doctest::Approx(sky.ambient + sky.sunIntensity));
    CHECK(skySample.direction.length() == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(skySample.direction.z > 0.5f); // dominated by up + high sun
}

TEST_CASE("shadow grid: density stays in the caster's own cell and accumulates") {
    ShadowVoxelGrid grid;
    ShadowVoxelGrid::Settings settings;
    settings.voxelSize = 1.0f;
    settings.shadowIntensity = 0.4f;

    const Vec3 caster{0.0f, 0.0f, 10.0f};
    grid.build({caster, caster}, settings);

    CHECK(grid.densityAt(caster) == doctest::Approx(0.8f));
    CHECK(grid.densityAt(caster + Vec3{0.0f, 0.0f, -1.0f}) == doctest::Approx(0.0f));
    CHECK(grid.densityAt(caster + Vec3{1.5f, 0.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(grid.densityAt(caster + Vec3{0.0f, 0.0f, 1.5f}) == doctest::Approx(0.0f));
}

TEST_CASE("shadow grid: transmittance drops under a canopy and is 1 in the open") {
    ShadowVoxelGrid grid;
    ShadowVoxelGrid::Settings settings;
    settings.voxelSize = 1.0f;
    settings.shadowIntensity = 1.0f;

    const Vec3 below{0.0f, 0.0f, 5.0f};
    std::vector<Vec3> canopy;
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            canopy.push_back(
                Vec3{static_cast<float>(x), static_cast<float>(y), 10.0f});
        }
    }
    // The probe is a caster too (buds are casters in vivo); its own voxel is
    // skipped by the ray walk, and it keeps the probe inside grid bounds.
    canopy.push_back(below);
    grid.build(canopy, settings);
    const float blocked = grid.transmittanceAlong(below, kUpAxis, 1.0f);
    CHECK(blocked < 0.7f);
    const float sideways = grid.transmittanceAlong(below, Vec3{1.0f, 0.0f, 0.0f}, 1.0f);
    CHECK(sideways == doctest::Approx(1.0f));
}

TEST_CASE("self-shading changes morphology: fewer internodes, outer-shell foliage") {
    GrowthParameters unshaded;
    unshaded.shadeModel = ShadeModel::Off;
    GrowthParameters shaded;
    shaded.shadeModel = ShadeModel::Raycast;

    const Plant open = grownPlant(42, 14, unshaded);
    const Plant competing = grownPlant(42, 14, shaded);

    CHECK(competing.graph.internodeCount() < open.graph.internodeCount());
    CHECK(outerShellBudFraction(competing) > outerShellBudFraction(open));

    std::string errors;
    CHECK_MESSAGE(competing.graph.validate(&errors), errors);
}

TEST_CASE("self-shading is deterministic") {
    const GrowthParameters parameters; // raycast shading is the default
    const Plant first = grownPlant(19, 16, parameters);
    const Plant second = grownPlant(19, 16, parameters);
    REQUIRE(first.graph.internodeCount() == second.graph.internodeCount());
    for (Index i = 0; i < static_cast<Index>(first.graph.internodeCount()); ++i) {
        CHECK(first.graph.internode(i).direction == second.graph.internode(i).direction);
        CHECK(first.graph.internode(i).radius == second.graph.internode(i).radius);
    }
}

TEST_CASE("raycast backend grows a valid tree distinct from the unshaded one") {
    GrowthParameters unshaded;
    unshaded.shadeModel = ShadeModel::Off;
    GrowthParameters raycast;
    raycast.shadeModel = ShadeModel::Raycast;

    const Plant openPlant = grownPlant(23, 12, unshaded);
    const Plant raycastPlant = grownPlant(23, 12, raycast);
    const Plant raycastAgain = grownPlant(23, 12, raycast);

    CHECK(raycastPlant.graph.internodeCount() > 1); // it grows
    CHECK(raycastPlant.graph.internodeCount() == raycastAgain.graph.internodeCount());
    CHECK(raycastPlant.graph.internodeCount() != openPlant.graph.internodeCount());

    std::string errors;
    CHECK_MESSAGE(raycastPlant.graph.validate(&errors), errors);
}

TEST_CASE("no sky is visible below the ground plane") {
    SkyEnvironment sky;
    CHECK(sky.sampleLight(Vec3{0.0f, 0.0f, 5.0f}).intensity > 0.0f);
    CHECK(sky.sampleLight(Vec3{0.0f, 0.0f, -0.5f}).intensity == 0.0f);
    CHECK(sky.sampleLight(Vec3{0.0f, 0.0f, -0.5f}).direction.isFinite());
}

TEST_CASE("raycast sky weights the zenith over the horizon") {
    // A caster directly overhead must cost more light than the same caster off
    // to the side: cosine weighting is what keeps a shaded understorey dark.
    GrowthParameters parameters;
    parameters.shadeModel = ShadeModel::Raycast;
    parameters.shadowIntensity = 1.0f;

    auto lightUnder = [&](const Vec3& casterOffset) {
        StemGraph graph;
        graph.addRoot(kUpAxis, 0.1f, 0.005f, 0);
        Plant plant;
        plant.graph = graph;
        GrowthContext context;
        ShadowVoxelGrid grid;
        ShadowVoxelGrid::Settings settings;
        settings.shadowIntensity = parameters.shadowIntensity;
        std::vector<Vec3> casters;
        casters.push_back(Vec3{}); // anchors the grid over the sample point
        for (int i = -6; i <= 6; ++i) {
            for (int j = -6; j <= 6; ++j) {
                casters.push_back(casterOffset +
                                  Vec3{static_cast<float>(i) * 0.4f, static_cast<float>(j) * 0.4f, 0.0f});
            }
        }
        grid.build(casters, settings);
        float transmitted = 0.0f;
        float weightTotal = 0.0f;
        for (std::uint32_t ray = 0; ray < 64; ++ray) {
            const float zenith = (static_cast<float>(ray) + 0.5f) / 64.0f;
            const float azimuth = 2.3999632f * static_cast<float>(ray);
            const float radial = std::sqrt(std::max(0.0f, 1.0f - zenith * zenith));
            const Vec3 direction{radial * std::cos(azimuth), radial * std::sin(azimuth), zenith};
            const float weight = direction.z;
            transmitted += grid.transmittanceAlong(Vec3{}, direction, 1.0f) * weight;
            weightTotal += weight;
        }
        return transmitted / weightTotal;
    };

    CHECK(lightUnder(Vec3{0.0f, 0.0f, 4.0f}) < lightUnder(Vec3{8.0f, 0.0f, 1.0f}));
}

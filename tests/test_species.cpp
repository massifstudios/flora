#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "flora/species/species.hpp"
#include "flora/species/species_io.hpp"

using namespace flora;

namespace {

bool approximately(float a, float b, float tolerance = 1.0e-5f) {
    return std::fabs(a - b) <= tolerance * std::fmax(1.0f, std::fmax(std::fabs(a), std::fabs(b)));
}

bool parametersApproximatelyEqual(const SpeciesParameter& a, const SpeciesParameter& b) {
    if (!approximately(a.distribution.mean, b.distribution.mean) ||
        !approximately(a.distribution.stddev, b.distribution.stddev) ||
        !approximately(a.distribution.minimum, b.distribution.minimum) ||
        !approximately(a.distribution.maximum, b.distribution.maximum)) {
        return false;
    }
    if (a.meanOverAge.points.size() != b.meanOverAge.points.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.meanOverAge.points.size(); ++i) {
        if (!approximately(a.meanOverAge.points[i].first, b.meanOverAge.points[i].first) ||
            !approximately(a.meanOverAge.points[i].second, b.meanOverAge.points[i].second)) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("default species materializes to default growth parameters") {
    const SpeciesDefinition species;
    const GrowthParameters materialized = materializeGrowthParameters(species, 123, 0);
    const GrowthParameters defaults;
    CHECK(materialized.baseGrowthLength == defaults.baseGrowthLength);
    CHECK(materialized.apicalControl == defaults.apicalControl);
    CHECK(materialized.branchAngle == defaults.branchAngle);
    CHECK(materialized.maxInternodes == defaults.maxInternodes);
}

TEST_CASE("materialization is deterministic per plant seed") {
    SpeciesDefinition species;
    species.baseGrowthLength.distribution.stddev = 0.1f;
    species.branchAngle.distribution.stddev = 0.1f;

    const GrowthParameters first = materializeGrowthParameters(species, 42, 5);
    const GrowthParameters second = materializeGrowthParameters(species, 42, 5);
    CHECK(first.baseGrowthLength == second.baseGrowthLength);
    CHECK(first.branchAngle == second.branchAngle);

    const GrowthParameters other = materializeGrowthParameters(species, 43, 5);
    CHECK(first.baseGrowthLength != other.baseGrowthLength);
}

TEST_CASE("individual variation is stable across ages") {
    SpeciesDefinition species;
    species.baseGrowthLength.distribution.stddev = 0.1f;
    const GrowthParameters young = materializeGrowthParameters(species, 42, 0);
    const GrowthParameters old = materializeGrowthParameters(species, 42, 50);
    // No age curve on this parameter: same individual, same value at any age.
    CHECK(young.baseGrowthLength == old.baseGrowthLength);
}

TEST_CASE("distribution clamps extreme draws") {
    SpeciesDefinition species;
    species.apicalControl.distribution = {0.6f, 10.0f, 0.5f, 0.7f};
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const GrowthParameters parameters = materializeGrowthParameters(species, seed, 0);
        CHECK(parameters.apicalControl >= 0.5f);
        CHECK(parameters.apicalControl <= 0.7f);
    }
}

TEST_CASE("age curves shift the mean over time") {
    SpeciesDefinition species;
    species.apicalControl.meanOverAge.points = {{0.0f, 0.8f}, {40.0f, 0.5f}};

    CHECK(materializeGrowthParameters(species, 1, 0).apicalControl == doctest::Approx(0.8f));
    CHECK(materializeGrowthParameters(species, 1, 20).apicalControl == doctest::Approx(0.65f));
    CHECK(materializeGrowthParameters(species, 1, 40).apicalControl == doctest::Approx(0.5f));
    CHECK(materializeGrowthParameters(species, 1, 100).apicalControl == doctest::Approx(0.5f));
}

TEST_CASE("minimal JSON yields defaults") {
    SpeciesDefinition species;
    std::string error;
    REQUIRE_MESSAGE(
        loadSpeciesFromJson(R"({"format": "flora-species", "version": 1})", species, &error),
        error);
    CHECK(species == SpeciesDefinition{});
}

TEST_CASE("shorthand numbers set the mean") {
    SpeciesDefinition species;
    std::string error;
    const char* json = R"({
        "format": "flora-species", "version": 1,
        "parameters": {"apicalControl": 0.8, "maxMetamersPerShoot": 5}
    })";
    REQUIRE_MESSAGE(loadSpeciesFromJson(json, species, &error), error);
    CHECK(species.apicalControl.distribution.mean == doctest::Approx(0.8f));
    CHECK(species.apicalControl.distribution.stddev == 0.0f);
    CHECK(species.maxMetamersPerShoot == 5);
}

TEST_CASE("angles convert from degrees to radians on load") {
    SpeciesDefinition species;
    std::string error;
    const char* json = R"({
        "format": "flora-species", "version": 1,
        "parameters": {"branchAngle": {"mean": 45.0, "stddev": 6.0}}
    })";
    REQUIRE_MESSAGE(loadSpeciesFromJson(json, species, &error), error);
    CHECK(species.branchAngle.distribution.mean == doctest::Approx(0.7853982f));
    CHECK(species.branchAngle.distribution.stddev == doctest::Approx(0.1047198f));
}

TEST_CASE("age curves parse and evaluate") {
    SpeciesDefinition species;
    std::string error;
    const char* json = R"({
        "format": "flora-species", "version": 1,
        "parameters": {
            "apicalControl": {"mean": 0.7, "overAge": [[0, 0.8], [30, 0.55]]}
        }
    })";
    REQUIRE_MESSAGE(loadSpeciesFromJson(json, species, &error), error);
    REQUIRE(species.apicalControl.meanOverAge.points.size() == 2);
    CHECK(species.apicalControl.value(0.0f, 15.0f) == doctest::Approx(0.675f));
}

TEST_CASE("invalid inputs are rejected with messages") {
    SpeciesDefinition species;
    std::string error;

    CHECK_FALSE(loadSpeciesFromJson("not json", species, &error));
    CHECK(error == "malformed JSON");

    CHECK_FALSE(loadSpeciesFromJson(R"({"version": 1})", species, &error));

    CHECK_FALSE(
        loadSpeciesFromJson(R"({"format": "flora-species", "version": 99})", species, &error));

    CHECK_FALSE(loadSpeciesFromJson(
        R"({"format": "flora-species", "version": 1, "parameters": {"noSuchParameter": 1}})",
        species, &error));
    CHECK(error.find("noSuchParameter") != std::string::npos);

    CHECK_FALSE(loadSpeciesFromJson(
        R"({"format": "flora-species", "version": 1,
            "parameters": {"apicalControl": {"mean": 0.5, "stddev": -1}}})",
        species, &error));

    CHECK_FALSE(loadSpeciesFromJson(
        R"({"format": "flora-species", "version": 1,
            "parameters": {"apicalControl": {"mean": 0.5, "min": 1.0, "max": 0.0}}})",
        species, &error));

    CHECK_FALSE(loadSpeciesFromJson(
        R"({"format": "flora-species", "version": 1,
            "parameters": {"apicalControl": {"mean": 0.5, "overAge": [[10, 1], [5, 2]]}}})",
        species, &error));

    CHECK_FALSE(loadSpeciesFromJson(
        R"({"format": "flora-species", "version": 1, "surprise": true})", species, &error));
}

TEST_CASE("save/load round-trip preserves the definition") {
    SpeciesDefinition original;
    original.name = "Round Trip";
    original.baseGrowthLength.distribution = {0.4f, 0.04f, 0.15f, 0.8f};
    original.apicalControl.distribution = {0.62f, 0.03f, 0.4f, 0.9f};
    original.apicalControl.meanOverAge.points = {{0.0f, 0.75f}, {15.0f, 0.62f}, {40.0f, 0.55f}};
    original.branchAngle.distribution = {0.66f, 0.1f, 0.26f, 1.31f};
    original.maxMetamersPerShoot = 4;
    original.lateralBudsPerMetamer = 2;

    const std::string saved = saveSpeciesToJson(original);
    SpeciesDefinition loaded;
    std::string error;
    REQUIRE_MESSAGE(loadSpeciesFromJson(saved, loaded, &error), error);

    CHECK(loaded.name == original.name);
    CHECK(loaded.maxMetamersPerShoot == original.maxMetamersPerShoot);
    CHECK(loaded.lateralBudsPerMetamer == original.lateralBudsPerMetamer);
    // Angle fields pass through a degree conversion; compare with tolerance.
    CHECK(parametersApproximatelyEqual(loaded.baseGrowthLength, original.baseGrowthLength));
    CHECK(parametersApproximatelyEqual(loaded.apicalControl, original.apicalControl));
    CHECK(parametersApproximatelyEqual(loaded.branchAngle, original.branchAngle));
}

TEST_CASE("species-driven growth varies between individuals") {
    SpeciesDefinition species;
    // vigorScale drives resource totals and therefore shoot counts; angle or
    // length variation alone would change geometry but not topology.
    species.vigorScale.distribution.stddev = 0.15f;

    auto grownWith = [&](std::uint64_t seed) {
        Plant plant;
        plant.seed = seed;
        plantSeedling(plant, materializeGrowthParameters(species, seed, 0));
        GrowthContext context;
        grow(plant, species, ConstantEnvironment{}, 14, context);
        return plant;
    };

    const Plant first = grownWith(100);
    const Plant second = grownWith(200);
    CHECK(first.graph.internodeCount() != second.graph.internodeCount());

    const Plant repeat = grownWith(100);
    CHECK(first.graph.internodeCount() == repeat.graph.internodeCount());
}

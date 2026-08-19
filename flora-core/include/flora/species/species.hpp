#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "flora/growth/parameters.hpp"
#include "flora/growth/simulation.hpp"
#include "flora/rng/random.hpp"

namespace flora {

// Species are data (ARCHITECTURE.md §5.2): parameters are distributions with
// optional mean-over-age curves. A species definition plus a plant seed
// deterministically materializes the GrowthParameters for that individual at
// a given age.

inline constexpr std::uint32_t kSpeciesSchemaVersion = 1;

// Normal distribution with clamping. stddev 0 collapses to the mean.
struct Distribution {
    float mean = 0.0f;
    float stddev = 0.0f;
    float minimum = -1.0e30f;
    float maximum = 1.0e30f;

    // Value for an individual whose variation z-score was drawn elsewhere;
    // meanValue overrides the stored mean (age curves shift it over time).
    float valueFor(float zScore, float meanValue) const;

    bool operator==(const Distribution&) const = default;
};

// Piecewise-linear curve over plant age in steps; clamped at both ends.
struct AgeCurve {
    std::vector<std::pair<float, float>> points; // (ageSteps, value), ascending age

    bool empty() const { return points.empty(); }
    float evaluate(float ageSteps) const;

    bool operator==(const AgeCurve&) const = default;
};

struct SpeciesParameter {
    Distribution distribution;
    AgeCurve meanOverAge; // optional; when present, replaces the mean by age

    float value(float zScore, float ageSteps) const;

    bool operator==(const SpeciesParameter&) const = default;
};

struct SpeciesDefinition {
    std::string name = "unnamed";

    // Float parameters (angles stored in radians internally; JSON uses degrees).
    SpeciesParameter baseGrowthLength{{0.35f, 0.0f, 0.01f, 5.0f}, {}};
    SpeciesParameter baseGrowthThickness{{0.01f, 0.0f, 0.0005f, 0.5f}, {}};
    SpeciesParameter apicalControl{{0.65f, 0.0f, 0.0f, 1.0f}, {}};
    SpeciesParameter vigorScale{{1.0f, 0.0f, 0.0f, 100.0f}, {}};
    SpeciesParameter activationThreshold{{0.45f, 0.0f, 0.0f, 1000.0f}, {}};
    SpeciesParameter branchAngle{{0.7853982f, 0.0f, 0.0f, 3.1415927f}, {}};
    SpeciesParameter branchAngleJitter{{0.0872665f, 0.0f, 0.0f, 3.1415927f}, {}};
    SpeciesParameter phyllotaxisAngle{{2.3999632f, 0.0f, 0.0f, 6.2831853f}, {}};

    // Integer parameters (fixed per species).
    std::uint32_t maxMetamersPerShoot = 3;
    std::uint32_t lateralBudsPerMetamer = 1;
    std::uint32_t budDormancyLimit = 3;
    std::uint32_t maxInternodes = 100000;

    bool operator==(const SpeciesDefinition&) const = default;
};

// Deterministic: individual variation z-scores are drawn from
// (plantSeed, RngPurpose::SpeciesVariation) in a fixed parameter order, so the
// same plant keeps its identity across ages and calls. Appending parameters
// appends draws; reordering is a simulation revision bump.
GrowthParameters materializeGrowthParameters(const SpeciesDefinition& species,
                                             std::uint64_t plantSeed, std::uint32_t ageSteps);

// Growth driven by a species definition: parameters are re-materialized each
// step so age curves apply, while per-individual variation stays fixed.
void grow(Plant& plant, const SpeciesDefinition& species, const ConstantEnvironment& environment,
          std::uint32_t steps, GrowthContext& context);

} // namespace flora

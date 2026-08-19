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

inline constexpr std::uint32_t kSpeciesSchemaVersion = 3;

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
    SpeciesParameter gravitropism{{0.08f, 0.0f, -1.0f, 1.0f}, {}};
    SpeciesParameter phototropism{{0.05f, 0.0f, 0.0f, 1.0f}, {}};
    SpeciesParameter gnarl{{0.06f, 0.0f, 0.0f, 1.0f}, {}};
    SpeciesParameter thicknessGrowthFactor{{1.5e-6f, 0.0f, 0.0f, 1.0e-3f}, {}};
    SpeciesParameter maintenanceCostPerVolume{{400.0f, 0.0f, 0.0f, 1.0e6f}, {}};
    SpeciesParameter dropWeakBranchRatio{{1.0f, 0.0f, 0.0f, 100.0f}, {}};
    SpeciesParameter maxShedFractionPerStep{{0.05f, 0.0f, 0.001f, 1.0f}, {}};
    // Schema v2: foliage density as shadow cast (shade cast and shade felt
    // scale together until per-species tolerance separates them).
    SpeciesParameter shadowIntensity{{0.2f, 0.0f, 0.0f, 2.0f}, {}};
    SpeciesParameter shadowFalloff{{2.0f, 0.0f, 1.01f, 10.0f}, {}};
    // Schema v2 (M7): foliage placement.
    SpeciesParameter leafMaxBearingRadius{{0.02f, 0.0f, 0.001f, 0.5f}, {}};
    SpeciesParameter leafClusterSpacing{{0.3f, 0.0f, 0.05f, 5.0f}, {}};
    SpeciesParameter leafClusterScale{{0.35f, 0.0f, 0.01f, 5.0f}, {}};
    SpeciesParameter leafDroop{{0.25f, 0.0f, -1.0f, 1.0f}, {}};
    SpeciesParameter leafProductivity{{7.0f, 0.0f, 0.0f, 1000.0f}, {}};
    // Schema v3: hydraulic limitation. maxPathLength is the species' mature
    // size ceiling (stem path, not straight-line height) with per-individual
    // variation; 0 removes the limit.
    SpeciesParameter maxPathLength{{40.0f, 0.0f, 0.0f, 200.0f}, {}};
    SpeciesParameter hydraulicFalloff{{2.0f, 0.0f, 0.25f, 8.0f}, {}};
    // Living shell thickness; heartwood inside it is free to maintain.
    SpeciesParameter sapwoodDepth{{0.05f, 0.0f, 0.002f, 1.0f}, {}};

    // Integer parameters (fixed per species).
    std::uint32_t maxMetamersPerShoot = 3;
    std::uint32_t lateralBudsPerMetamer = 1;
    std::uint32_t budDormancyLimit = 3;
    std::uint32_t branchDecaySteps = 4; // 0: deadwood never removed
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
// shadeModel is a fidelity setting layered on top of the species biology.
void grow(Plant& plant, const SpeciesDefinition& species, const Environment& environment,
          std::uint32_t steps, GrowthContext& context,
          ShadeModel shadeModel = ShadeModel::VoxelPropagation);

} // namespace flora

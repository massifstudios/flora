#include "flora/species/species.hpp"

#include <algorithm>

namespace flora {

float Distribution::valueFor(float zScore, float meanValue) const {
    return std::clamp(meanValue + zScore * stddev, minimum, maximum);
}

float AgeCurve::evaluate(float ageSteps) const {
    if (points.empty()) {
        return 0.0f;
    }
    if (ageSteps <= points.front().first) {
        return points.front().second;
    }
    if (ageSteps >= points.back().first) {
        return points.back().second;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (ageSteps <= points[i].first) {
            const auto& [age0, value0] = points[i - 1];
            const auto& [age1, value1] = points[i];
            const float span = age1 - age0;
            const float blend = span > 0.0f ? (ageSteps - age0) / span : 0.0f;
            return value0 + (value1 - value0) * blend;
        }
    }
    return points.back().second;
}

float SpeciesParameter::value(float zScore, float ageSteps) const {
    const float mean = meanOverAge.empty() ? distribution.mean : meanOverAge.evaluate(ageSteps);
    return distribution.valueFor(zScore, mean);
}

GrowthParameters materializeGrowthParameters(const SpeciesDefinition& species,
                                             std::uint64_t plantSeed, std::uint32_t ageSteps) {
    auto stream = RandomStream::derive(plantSeed, RngPurpose::SpeciesVariation);
    const float age = static_cast<float>(ageSteps);

    // Fixed draw order; append only (species.hpp).
    GrowthParameters parameters;
    parameters.baseGrowthLength = species.baseGrowthLength.value(stream.normal(0.0f, 1.0f), age);
    parameters.baseGrowthThickness =
        species.baseGrowthThickness.value(stream.normal(0.0f, 1.0f), age);
    parameters.apicalControl = species.apicalControl.value(stream.normal(0.0f, 1.0f), age);
    parameters.vigorScale = species.vigorScale.value(stream.normal(0.0f, 1.0f), age);
    parameters.activationThreshold =
        species.activationThreshold.value(stream.normal(0.0f, 1.0f), age);
    parameters.branchAngle = species.branchAngle.value(stream.normal(0.0f, 1.0f), age);
    parameters.branchAngleJitter =
        species.branchAngleJitter.value(stream.normal(0.0f, 1.0f), age);
    parameters.phyllotaxisAngle = species.phyllotaxisAngle.value(stream.normal(0.0f, 1.0f), age);
    parameters.gravitropism = species.gravitropism.value(stream.normal(0.0f, 1.0f), age);
    parameters.phototropism = species.phototropism.value(stream.normal(0.0f, 1.0f), age);
    parameters.gnarl = species.gnarl.value(stream.normal(0.0f, 1.0f), age);
    parameters.thicknessGrowthFactor =
        species.thicknessGrowthFactor.value(stream.normal(0.0f, 1.0f), age);
    parameters.maintenanceCostPerVolume =
        species.maintenanceCostPerVolume.value(stream.normal(0.0f, 1.0f), age);
    parameters.dropWeakBranchRatio =
        species.dropWeakBranchRatio.value(stream.normal(0.0f, 1.0f), age);
    parameters.maxShedFractionPerStep =
        species.maxShedFractionPerStep.value(stream.normal(0.0f, 1.0f), age);
    parameters.shadowIntensity = species.shadowIntensity.value(stream.normal(0.0f, 1.0f), age);
    parameters.shadowFalloff = species.shadowFalloff.value(stream.normal(0.0f, 1.0f), age);
    parameters.leafMaxBearingRadius =
        species.leafMaxBearingRadius.value(stream.normal(0.0f, 1.0f), age);
    parameters.leafClusterSpacing =
        species.leafClusterSpacing.value(stream.normal(0.0f, 1.0f), age);
    parameters.leafClusterScale = species.leafClusterScale.value(stream.normal(0.0f, 1.0f), age);
    parameters.leafDroop = species.leafDroop.value(stream.normal(0.0f, 1.0f), age);
    parameters.leafProductivity = species.leafProductivity.value(stream.normal(0.0f, 1.0f), age);
    parameters.maxPathLength = species.maxPathLength.value(stream.normal(0.0f, 1.0f), age);
    parameters.hydraulicFalloff = species.hydraulicFalloff.value(stream.normal(0.0f, 1.0f), age);
    parameters.sapwoodDepth = species.sapwoodDepth.value(stream.normal(0.0f, 1.0f), age);
    parameters.lateralGravitropism =
        species.lateralGravitropism.value(stream.normal(0.0f, 1.0f), age);
    parameters.lateralPhototropism =
        species.lateralPhototropism.value(stream.normal(0.0f, 1.0f), age);
    parameters.apicalDominanceRange =
        species.apicalDominanceRange.value(stream.normal(0.0f, 1.0f), age);
    parameters.branchVigorFalloff =
        species.branchVigorFalloff.value(stream.normal(0.0f, 1.0f), age);
    parameters.shadowVerticalRetention =
        species.shadowVerticalRetention.value(stream.normal(0.0f, 1.0f), age);
    parameters.shadowLateralSpread =
        species.shadowLateralSpread.value(stream.normal(0.0f, 1.0f), age);
    parameters.thicknessApicalWeight =
        species.thicknessApicalWeight.value(stream.normal(0.0f, 1.0f), age);
    parameters.dropShadedGrowthLight =
        species.dropShadedGrowthLight.value(stream.normal(0.0f, 1.0f), age);
    parameters.brightGrowthPriority =
        species.brightGrowthPriority.value(stream.normal(0.0f, 1.0f), age);
    parameters.branchStiffness = species.branchStiffness.value(stream.normal(0.0f, 1.0f), age);
    parameters.bendIntegration = species.bendIntegration.value(stream.normal(0.0f, 1.0f), age);
    parameters.leafBearingTipDistance =
        species.leafBearingTipDistance.value(stream.normal(0.0f, 1.0f), age);
    parameters.lateralSetPointAngle =
        species.lateralSetPointAngle.value(stream.normal(0.0f, 1.0f), age);
    parameters.lateralSetPointStrength =
        species.lateralSetPointStrength.value(stream.normal(0.0f, 1.0f), age);

    parameters.maxMetamersPerShoot = species.maxMetamersPerShoot;
    parameters.lateralBudsPerMetamer = species.lateralBudsPerMetamer;
    parameters.budDormancyLimit = species.budDormancyLimit;
    parameters.branchDecaySteps = species.branchDecaySteps;
    parameters.maxInternodes = species.maxInternodes;
    return parameters;
}

void grow(Plant& plant, const SpeciesDefinition& species, const Environment& environment,
          std::uint32_t steps, GrowthContext& context, ShadeModel shadeModel) {
    for (std::uint32_t i = 0; i < steps; ++i) {
        GrowthParameters parameters =
            materializeGrowthParameters(species, plant.seed, plant.ageSteps);
        parameters.shadeModel = shadeModel;
        growStep(plant, parameters, environment, context);
    }
}

} // namespace flora

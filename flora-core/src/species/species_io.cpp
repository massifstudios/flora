#include "flora/species/species_io.hpp"

#include <array>
#include <cmath>

#include <nlohmann/json.hpp>

namespace flora {

namespace {

using Json = nlohmann::ordered_json;

constexpr double kDegreesToRadians = 0.017453292519943295;
constexpr double kRadiansToDegrees = 57.29577951308232;

struct FloatEntry {
    const char* name;
    SpeciesParameter SpeciesDefinition::* member;
    bool isAngle; // degrees in JSON, radians in memory
};

constexpr std::array<FloatEntry, 31> kFloatEntries = {{
    {"baseGrowthLength", &SpeciesDefinition::baseGrowthLength, false},
    {"baseGrowthThickness", &SpeciesDefinition::baseGrowthThickness, false},
    {"apicalControl", &SpeciesDefinition::apicalControl, false},
    {"vigorScale", &SpeciesDefinition::vigorScale, false},
    {"activationThreshold", &SpeciesDefinition::activationThreshold, false},
    {"branchAngle", &SpeciesDefinition::branchAngle, true},
    {"branchAngleJitter", &SpeciesDefinition::branchAngleJitter, true},
    {"phyllotaxisAngle", &SpeciesDefinition::phyllotaxisAngle, true},
    {"gravitropism", &SpeciesDefinition::gravitropism, false},
    {"phototropism", &SpeciesDefinition::phototropism, false},
    {"gnarl", &SpeciesDefinition::gnarl, false},
    {"thicknessGrowthFactor", &SpeciesDefinition::thicknessGrowthFactor, false},
    {"maintenanceCostPerVolume", &SpeciesDefinition::maintenanceCostPerVolume, false},
    {"dropWeakBranchRatio", &SpeciesDefinition::dropWeakBranchRatio, false},
    {"maxShedFractionPerStep", &SpeciesDefinition::maxShedFractionPerStep, false},
    {"shadowIntensity", &SpeciesDefinition::shadowIntensity, false},
    {"shadowFalloff", &SpeciesDefinition::shadowFalloff, false},
    {"leafMaxBearingRadius", &SpeciesDefinition::leafMaxBearingRadius, false},
    {"leafClusterSpacing", &SpeciesDefinition::leafClusterSpacing, false},
    {"leafClusterScale", &SpeciesDefinition::leafClusterScale, false},
    {"leafDroop", &SpeciesDefinition::leafDroop, false},
    {"leafProductivity", &SpeciesDefinition::leafProductivity, false},
    {"maxPathLength", &SpeciesDefinition::maxPathLength, false},
    {"hydraulicFalloff", &SpeciesDefinition::hydraulicFalloff, false},
    {"sapwoodDepth", &SpeciesDefinition::sapwoodDepth, false},
    {"lateralGravitropism", &SpeciesDefinition::lateralGravitropism, false},
    {"lateralPhototropism", &SpeciesDefinition::lateralPhototropism, false},
    {"apicalDominanceRange", &SpeciesDefinition::apicalDominanceRange, false},
    {"branchVigorFalloff", &SpeciesDefinition::branchVigorFalloff, false},
    {"shadowVerticalRetention", &SpeciesDefinition::shadowVerticalRetention, false},
    {"shadowLateralSpread", &SpeciesDefinition::shadowLateralSpread, false},
}};

struct IntegerEntry {
    const char* name;
    std::uint32_t SpeciesDefinition::* member;
};

constexpr std::array<IntegerEntry, 5> kIntegerEntries = {{
    {"maxMetamersPerShoot", &SpeciesDefinition::maxMetamersPerShoot},
    {"lateralBudsPerMetamer", &SpeciesDefinition::lateralBudsPerMetamer},
    {"budDormancyLimit", &SpeciesDefinition::budDormancyLimit},
    {"branchDecaySteps", &SpeciesDefinition::branchDecaySteps},
    {"maxInternodes", &SpeciesDefinition::maxInternodes},
}};

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

float toInternal(double jsonValue, bool isAngle) {
    return static_cast<float>(isAngle ? jsonValue * kDegreesToRadians : jsonValue);
}

double toJson(float internalValue, bool isAngle) {
    const double value = static_cast<double>(internalValue);
    return isAngle ? value * kRadiansToDegrees : value;
}

bool parseFloatParameter(const Json& value, const FloatEntry& entry, SpeciesParameter& parameter,
                         std::string* error) {
    if (value.is_number()) {
        parameter.distribution.mean = toInternal(value.get<double>(), entry.isAngle);
        parameter.distribution.stddev = 0.0f;
        parameter.meanOverAge.points.clear();
        return true;
    }
    if (!value.is_object()) {
        return fail(error, std::string("parameter '") + entry.name +
                               "' must be a number or an object");
    }
    for (const auto& [key, item] : value.items()) {
        if (key == "mean") {
            if (!item.is_number()) {
                return fail(error, std::string("'mean' of '") + entry.name + "' must be a number");
            }
            parameter.distribution.mean = toInternal(item.get<double>(), entry.isAngle);
        } else if (key == "stddev") {
            if (!item.is_number()) {
                return fail(error,
                            std::string("'stddev' of '") + entry.name + "' must be a number");
            }
            parameter.distribution.stddev = toInternal(item.get<double>(), entry.isAngle);
        } else if (key == "min") {
            if (!item.is_number()) {
                return fail(error, std::string("'min' of '") + entry.name + "' must be a number");
            }
            parameter.distribution.minimum = toInternal(item.get<double>(), entry.isAngle);
        } else if (key == "max") {
            if (!item.is_number()) {
                return fail(error, std::string("'max' of '") + entry.name + "' must be a number");
            }
            parameter.distribution.maximum = toInternal(item.get<double>(), entry.isAngle);
        } else if (key == "overAge") {
            if (!item.is_array() || item.empty()) {
                return fail(error, std::string("'overAge' of '") + entry.name +
                                       "' must be a non-empty array of [age, value] pairs");
            }
            parameter.meanOverAge.points.clear();
            float previousAge = -1.0e30f;
            for (const Json& point : item) {
                if (!point.is_array() || point.size() != 2 || !point[0].is_number() ||
                    !point[1].is_number()) {
                    return fail(error, std::string("'overAge' of '") + entry.name +
                                           "' entries must be [age, value] number pairs");
                }
                const float age = point[0].get<float>();
                if (age <= previousAge) {
                    return fail(error, std::string("'overAge' of '") + entry.name +
                                           "' ages must be strictly ascending");
                }
                previousAge = age;
                parameter.meanOverAge.points.emplace_back(
                    age, toInternal(point[1].get<double>(), entry.isAngle));
            }
        } else {
            return fail(error,
                        std::string("unknown key '") + key + "' in parameter '" + entry.name + "'");
        }
    }
    if (parameter.distribution.stddev < 0.0f) {
        return fail(error, std::string("'stddev' of '") + entry.name + "' must be non-negative");
    }
    if (parameter.distribution.minimum > parameter.distribution.maximum) {
        return fail(error, std::string("'min' of '") + entry.name + "' exceeds 'max'");
    }
    return true;
}

} // namespace

bool loadSpeciesFromJson(const std::string& jsonText, SpeciesDefinition& species,
                         std::string* error) {
    Json root = Json::parse(jsonText, nullptr, false);
    if (root.is_discarded()) {
        return fail(error, "malformed JSON");
    }
    if (!root.is_object()) {
        return fail(error, "root must be an object");
    }

    SpeciesDefinition result; // defaults; JSON overrides

    for (const auto& [key, value] : root.items()) {
        if (key == "format") {
            if (!value.is_string() || value.get<std::string>() != "flora-species") {
                return fail(error, "'format' must be \"flora-species\"");
            }
        } else if (key == "version") {
            if (!value.is_number_unsigned() || value.get<std::uint32_t>() == 0 ||
                value.get<std::uint32_t>() > kSpeciesSchemaVersion) {
                return fail(error, "unsupported schema version");
            }
        } else if (key == "name") {
            if (!value.is_string()) {
                return fail(error, "'name' must be a string");
            }
            result.name = value.get<std::string>();
        } else if (key == "parameters") {
            if (!value.is_object()) {
                return fail(error, "'parameters' must be an object");
            }
            for (const auto& [parameterName, parameterValue] : value.items()) {
                bool known = false;
                for (const FloatEntry& entry : kFloatEntries) {
                    if (parameterName == entry.name) {
                        if (!parseFloatParameter(parameterValue, entry, result.*(entry.member),
                                                 error)) {
                            return false;
                        }
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    for (const IntegerEntry& entry : kIntegerEntries) {
                        if (parameterName == entry.name) {
                            if (!parameterValue.is_number_unsigned()) {
                                return fail(error, std::string("parameter '") + parameterName +
                                                       "' must be an unsigned integer");
                            }
                            result.*(entry.member) = parameterValue.get<std::uint32_t>();
                            known = true;
                            break;
                        }
                    }
                }
                if (!known) {
                    return fail(error, std::string("unknown parameter '") + parameterName + "'");
                }
            }
        } else {
            return fail(error, std::string("unknown key '") + key + "'");
        }
    }

    if (!root.contains("format") || !root.contains("version")) {
        return fail(error, "'format' and 'version' are required");
    }

    species = std::move(result);
    return true;
}

std::string saveSpeciesToJson(const SpeciesDefinition& species) {
    Json root;
    root["format"] = "flora-species";
    root["version"] = kSpeciesSchemaVersion;
    root["name"] = species.name;

    Json parameters;
    for (const FloatEntry& entry : kFloatEntries) {
        const SpeciesParameter& parameter = species.*(entry.member);
        Json value;
        value["mean"] = toJson(parameter.distribution.mean, entry.isAngle);
        value["stddev"] = toJson(parameter.distribution.stddev, entry.isAngle);
        value["min"] = toJson(parameter.distribution.minimum, entry.isAngle);
        value["max"] = toJson(parameter.distribution.maximum, entry.isAngle);
        if (!parameter.meanOverAge.empty()) {
            Json curve = Json::array();
            for (const auto& [age, mean] : parameter.meanOverAge.points) {
                curve.push_back({age, toJson(mean, entry.isAngle)});
            }
            value["overAge"] = std::move(curve);
        }
        parameters[entry.name] = std::move(value);
    }
    for (const IntegerEntry& entry : kIntegerEntries) {
        parameters[entry.name] = species.*(entry.member);
    }
    root["parameters"] = std::move(parameters);
    return root.dump(2);
}

} // namespace flora

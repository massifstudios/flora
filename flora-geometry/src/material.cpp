#include "flora/geometry/material.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace flora {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::uint32_t kMaterialSchemaVersion = 3;

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool parseString(const Json& value, const char* key, std::string& target, std::string* error) {
    if (!value.is_string()) {
        return fail(error, std::string("'") + key + "' must be a string");
    }
    target = value.get<std::string>();
    return true;
}

bool parseNumber(const Json& value, const char* key, float& target, std::string* error) {
    if (!value.is_number()) {
        return fail(error, std::string("'") + key + "' must be a number");
    }
    target = value.get<float>();
    return true;
}

// Shared texture keys; extra keys are handled by the caller's block parser.
bool parseMapKey(const std::string& key, const Json& value, TextureMaps& maps, bool& consumed,
                 std::string* error) {
    consumed = true;
    if (key == "color") {
        return parseString(value, "color", maps.color, error);
    }
    if (key == "normal") {
        return parseString(value, "normal", maps.normal, error);
    }
    if (key == "roughness") {
        return parseString(value, "roughness", maps.roughness, error);
    }
    if (key == "depth") {
        return parseString(value, "depth", maps.depth, error);
    }
    consumed = false;
    return true;
}

bool parseUnsigned(const Json& value, const char* key, std::uint32_t& target, std::string* error) {
    if (!value.is_number_unsigned()) {
        return fail(error, std::string("'") + key + "' must be a non-negative integer");
    }
    target = value.get<std::uint32_t>();
    return true;
}

bool parseFoliageVariant(const Json& value, FoliageVariant& variant, std::string* error) {
    if (!value.is_object()) {
        return fail(error, "each entry of 'variants' must be an object");
    }
    TwigCardVariant& selector = variant.selector;
    for (const auto& [key, entry] : value.items()) {
        bool consumed = false;
        if (!parseMapKey(key, entry, variant.maps, consumed, error)) {
            return false;
        }
        if (consumed) {
            continue;
        }
        bool ok = true;
        if (key == "minDensity") {
            ok = parseNumber(entry, "minDensity", selector.minFoliageDensity, error);
        } else if (key == "maxDensity") {
            ok = parseNumber(entry, "maxDensity", selector.maxFoliageDensity, error);
        } else if (key == "minClusters") {
            ok = parseUnsigned(entry, "minClusters", selector.minClusters, error);
        } else if (key == "maxClusters") {
            ok = parseUnsigned(entry, "maxClusters", selector.maxClusters, error);
        } else if (key == "minCrownDepth") {
            ok = parseNumber(entry, "minCrownDepth", selector.minCrownDepth, error);
        } else if (key == "maxCrownDepth") {
            ok = parseNumber(entry, "maxCrownDepth", selector.maxCrownDepth, error);
        } else if (key == "minBranchFraction") {
            ok = parseNumber(entry, "minBranchFraction", selector.minBranchFraction, error);
        } else if (key == "maxBranchFraction") {
            ok = parseNumber(entry, "maxBranchFraction", selector.maxBranchFraction, error);
        } else if (key == "weight") {
            ok = parseNumber(entry, "weight", selector.weight, error);
        } else {
            return fail(error, "unknown key '" + key + "' in 'variants'");
        }
        if (!ok) {
            return false;
        }
    }
    if (variant.maps.color.empty()) {
        return fail(error, "each entry of 'variants' needs a 'color'");
    }
    if (selector.minFoliageDensity > selector.maxFoliageDensity ||
        selector.minClusters > selector.maxClusters ||
        selector.minCrownDepth > selector.maxCrownDepth ||
        selector.minBranchFraction > selector.maxBranchFraction) {
        return fail(error, "variant '" + variant.maps.color + "' has an empty condition range");
    }
    if (selector.weight < 0.0f) {
        return fail(error, "variant 'weight' must not be negative");
    }
    return true;
}

bool parseFoliageBlock(const Json& value, MaterialSet& result, std::string* error) {
    for (const auto& [key, entry] : value.items()) {
        bool ok = true;
        if (key == "aspect") {
            ok = parseNumber(entry, "aspect", result.foliageAspect, error);
        } else if (key == "pivotU") {
            ok = parseNumber(entry, "pivotU", result.foliagePivotU, error);
        } else if (key == "alphaCutoff") {
            ok = parseNumber(entry, "alphaCutoff", result.foliageAlphaCutoff, error);
        } else if (key == "sizeScale") {
            ok = parseNumber(entry, "sizeScale", result.foliageSizeScale, error);
        } else if (key == "minHeight") {
            ok = parseNumber(entry, "minHeight", result.foliageMinHeight, error);
        } else if (key == "maxLength") {
            ok = parseNumber(entry, "maxLength", result.foliageMaxLength, error);
        } else if (key == "instances") {
            ok = parseUnsigned(entry, "instances", result.foliageInstances, error);
        } else if (key == "instanceSpread") {
            ok = parseNumber(entry, "instanceSpread", result.foliageInstanceSpread, error);
        } else if (key == "instanceScaleJitter") {
            ok = parseNumber(entry, "instanceScaleJitter", result.foliageInstanceScaleJitter, error);
        } else if (key == "variants") {
            if (!entry.is_array() || entry.empty()) {
                return fail(error, "'variants' must be a non-empty array");
            }
            for (const Json& item : entry) {
                FoliageVariant variant;
                if (!parseFoliageVariant(item, variant, error)) {
                    return false;
                }
                result.foliage.push_back(std::move(variant));
            }
        } else {
            return fail(error, "unknown key '" + key + "' in 'foliage'");
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string directoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

// Collapses "a/../b" so joined paths stay readable for the tools that consume
// them; purely textual, no filesystem access.
std::string collapseParentSteps(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        std::string part = path.substr(start, end - start);
        if (part == ".." && !parts.empty() && parts.back() != ".." && !parts.back().empty()) {
            parts.pop_back();
        } else if (part != ".") {
            parts.push_back(std::move(part));
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        result += parts[i];
        if (i + 1 < parts.size()) {
            result += '/';
        }
    }
    return result;
}

void prefixIfRelative(std::string& path, const std::string& prefix) {
    if (path.empty() || prefix.empty() || path.front() == '/') {
        return;
    }
    path = collapseParentSteps(prefix + path);
}

void appendMap(std::ostringstream& out, const char* directive, const std::string& path,
               const std::string& prefix) {
    if (path.empty()) {
        return;
    }
    std::string full = path;
    prefixIfRelative(full, prefix);
    out << directive << ' ' << full << '\n';
}

} // namespace

bool loadMaterialSetFromJson(const std::string& jsonText, MaterialSet& materials,
                             std::string* error) {
    Json root = Json::parse(jsonText, nullptr, false);
    if (root.is_discarded()) {
        return fail(error, "malformed JSON");
    }
    if (!root.is_object()) {
        return fail(error, "root must be an object");
    }
    if (!root.contains("format") || !root.contains("version")) {
        return fail(error, "'format' and 'version' are required");
    }

    MaterialSet result;
    for (const auto& [key, value] : root.items()) {
        if (key == "format") {
            if (!value.is_string() || value.get<std::string>() != "flora-materials") {
                return fail(error, "'format' must be \"flora-materials\"");
            }
        } else if (key == "version") {
            // Development reset: exactly the current version, so a stale file
            // fails loudly instead of being read under the wrong meaning.
            if (!value.is_number_unsigned() ||
                value.get<std::uint32_t>() != kMaterialSchemaVersion) {
                return fail(error, "material schema version must be " +
                                       std::to_string(kMaterialSchemaVersion));
            }
        } else if (key == "name") {
            if (!parseString(value, "name", result.name, error)) {
                return false;
            }
        } else if (key == "bark") {
            if (!value.is_object()) {
                return fail(error, "'bark' must be an object");
            }
            for (const auto& [mapKey, mapValue] : value.items()) {
                bool consumed = false;
                if (!parseMapKey(mapKey, mapValue, result.bark, consumed, error)) {
                    return false;
                }
                if (consumed) {
                    continue;
                }
                if (mapKey != "uvLengthScale") {
                    return fail(error, "unknown key '" + mapKey + "' in 'bark'");
                }
                if (!parseNumber(mapValue, "uvLengthScale", result.barkUvLengthScale, error)) {
                    return false;
                }
            }
        } else if (key == "foliage") {
            if (!value.is_object()) {
                return fail(error, "'foliage' must be an object");
            }
            if (!parseFoliageBlock(value, result, error)) {
                return false;
            }
        } else {
            return fail(error, "unknown key '" + key + "'");
        }
    }

    if (result.foliage.empty()) {
        return fail(error, "'foliage' needs a 'variants' list");
    }
    if (result.foliageAspect <= 0.0f || result.barkUvLengthScale <= 0.0f) {
        return fail(error, "'aspect' and 'uvLengthScale' must be positive");
    }
    if (result.foliageSizeScale <= 0.0f || result.foliageMinHeight <= 0.0f ||
        result.foliageMaxLength < result.foliageMinHeight) {
        return fail(error, "'sizeScale' and 'minHeight' must be positive and 'maxLength' "
                           "at least 'minHeight'");
    }
    if (result.foliageInstances == 0) {
        return fail(error, "'instances' must be at least 1");
    }
    if (result.foliageInstanceSpread < 0.0f || result.foliageInstanceScaleJitter < 0.0f ||
        result.foliageInstanceScaleJitter >= 1.0f) {
        return fail(error, "'instanceSpread' must not be negative and 'instanceScaleJitter' "
                           "must be in [0, 1)");
    }
    materials = std::move(result);
    return true;
}

bool loadMaterialSetFromFile(const std::string& path, MaterialSet& materials, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        return fail(error, "cannot open material file: " + path);
    }
    std::ostringstream text;
    text << file.rdbuf();
    if (!loadMaterialSetFromJson(text.str(), materials, error)) {
        return false;
    }
    const std::string directory = directoryOf(path);
    auto reroot = [&directory](TextureMaps& maps) {
        prefixIfRelative(maps.color, directory);
        prefixIfRelative(maps.normal, directory);
        prefixIfRelative(maps.roughness, directory);
        prefixIfRelative(maps.depth, directory);
    };
    reroot(materials.bark);
    for (FoliageVariant& variant : materials.foliage) {
        reroot(variant.maps);
    }
    return true;
}

std::string foliageMaterialName(const MaterialSet& materials, std::size_t variant) {
    return materials.foliage.size() <= 1 ? std::string("foliage")
                                         : "foliage_" + std::to_string(variant);
}

std::vector<TwigCardVariant> foliageSelectors(const MaterialSet& materials) {
    std::vector<TwigCardVariant> selectors;
    selectors.reserve(materials.foliage.size());
    for (const FoliageVariant& variant : materials.foliage) {
        selectors.push_back(variant.selector);
    }
    return selectors;
}

std::string materialLibraryText(const MaterialSet& materials, const std::string& texturePathPrefix) {
    std::ostringstream out;
    out << "# flora material library: " << materials.name << '\n';

    out << "\nnewmtl bark\n";
    out << "Kd 0.42 0.33 0.25\nKs 0.02 0.02 0.02\nNs 8\nd 1\nillum 2\n";
    appendMap(out, "map_Kd", materials.bark.color, texturePathPrefix);
    appendMap(out, "norm", materials.bark.normal, texturePathPrefix);
    appendMap(out, "map_Ns", materials.bark.roughness, texturePathPrefix);
    appendMap(out, "disp", materials.bark.depth, texturePathPrefix);

    for (std::size_t i = 0; i < materials.foliage.size(); ++i) {
        const TextureMaps& maps = materials.foliage[i].maps;
        out << "\nnewmtl " << foliageMaterialName(materials, i) << '\n';
        out << "Kd 0.30 0.42 0.20\nKs 0.03 0.03 0.03\nNs 12\nd 1\nillum 2\n";
        appendMap(out, "map_Kd", maps.color, texturePathPrefix);
        // Alpha from the colour map's own channel; cutoff is a renderer setting.
        appendMap(out, "map_d", maps.color, texturePathPrefix);
        appendMap(out, "norm", maps.normal, texturePathPrefix);
        appendMap(out, "map_Ns", maps.roughness, texturePathPrefix);
        appendMap(out, "disp", maps.depth, texturePathPrefix);
    }
    return out.str();
}

TwigCardParameters twigCardParameters(const MaterialSet& materials,
                                      const GrowthParameters& growth, std::uint64_t seed) {
    TwigCardParameters parameters;
    parameters.maxBearingRadius = growth.leafMaxBearingRadius;
    parameters.clusterSpacing = growth.leafClusterSpacing;
    parameters.aspect = materials.foliageAspect;
    parameters.pivotU = materials.foliagePivotU;
    parameters.sizeScale = materials.foliageSizeScale;
    parameters.minHeight = materials.foliageMinHeight;
    parameters.maxLength = materials.foliageMaxLength;
    parameters.instancesPerCard = materials.foliageInstances;
    parameters.instanceSpread = materials.foliageInstanceSpread;
    parameters.instanceScaleJitter = materials.foliageInstanceScaleJitter;
    parameters.variants = foliageSelectors(materials);
    parameters.seed = seed;
    return parameters;
}

} // namespace flora

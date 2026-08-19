#include "flora/geometry/material.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace flora {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::uint32_t kMaterialSchemaVersion = 1;

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
    consumed = false;
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
            if (!value.is_number_unsigned() || value.get<std::uint32_t>() == 0 ||
                value.get<std::uint32_t>() > kMaterialSchemaVersion) {
                return fail(error, "unsupported material schema version");
            }
        } else if (key == "name") {
            if (!parseString(value, "name", result.name, error)) {
                return false;
            }
        } else if (key == "bark" || key == "foliage") {
            if (!value.is_object()) {
                return fail(error, "'" + key + "' must be an object");
            }
            const bool isBark = key == "bark";
            TextureMaps& maps = isBark ? result.bark : result.foliage;
            for (const auto& [mapKey, mapValue] : value.items()) {
                bool consumed = false;
                if (!parseMapKey(mapKey, mapValue, maps, consumed, error)) {
                    return false;
                }
                if (consumed) {
                    continue;
                }
                if (isBark && mapKey == "uvLengthScale") {
                    if (!parseNumber(mapValue, "uvLengthScale", result.barkUvLengthScale, error)) {
                        return false;
                    }
                } else if (!isBark && mapKey == "aspect") {
                    if (!parseNumber(mapValue, "aspect", result.foliageAspect, error)) {
                        return false;
                    }
                } else if (!isBark && mapKey == "pivotU") {
                    if (!parseNumber(mapValue, "pivotU", result.foliagePivotU, error)) {
                        return false;
                    }
                } else if (!isBark && mapKey == "alphaCutoff") {
                    if (!parseNumber(mapValue, "alphaCutoff", result.foliageAlphaCutoff, error)) {
                        return false;
                    }
                } else {
                    return fail(error, "unknown key '" + mapKey + "' in '" + key + "'");
                }
            }
        } else {
            return fail(error, "unknown key '" + key + "'");
        }
    }

    if (result.foliageAspect <= 0.0f || result.barkUvLengthScale <= 0.0f) {
        return fail(error, "'aspect' and 'uvLengthScale' must be positive");
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
    for (TextureMaps* maps : {&materials.bark, &materials.foliage}) {
        prefixIfRelative(maps->color, directory);
        prefixIfRelative(maps->normal, directory);
        prefixIfRelative(maps->roughness, directory);
    }
    return true;
}

std::string materialLibraryText(const MaterialSet& materials, const std::string& texturePathPrefix) {
    std::ostringstream out;
    out << "# flora material library: " << materials.name << '\n';

    out << "\nnewmtl bark\n";
    out << "Kd 0.42 0.33 0.25\nKs 0.02 0.02 0.02\nNs 8\nd 1\nillum 2\n";
    appendMap(out, "map_Kd", materials.bark.color, texturePathPrefix);
    appendMap(out, "norm", materials.bark.normal, texturePathPrefix);
    appendMap(out, "map_Ns", materials.bark.roughness, texturePathPrefix);

    out << "\nnewmtl foliage\n";
    out << "Kd 0.30 0.42 0.20\nKs 0.03 0.03 0.03\nNs 12\nd 1\nillum 2\n";
    appendMap(out, "map_Kd", materials.foliage.color, texturePathPrefix);
    // Alpha from the colour map's own channel; cutoff is a renderer setting.
    appendMap(out, "map_d", materials.foliage.color, texturePathPrefix);
    appendMap(out, "norm", materials.foliage.normal, texturePathPrefix);
    appendMap(out, "map_Ns", materials.foliage.roughness, texturePathPrefix);
    return out.str();
}

} // namespace flora

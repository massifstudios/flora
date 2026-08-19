#pragma once

#include <string>

namespace flora {

// Texture references for generated geometry. Presentation data, kept out of
// species definitions: a species is simulation biology, a material set is one
// possible look for it (ARCHITECTURE.md §9). Paths are relative to the
// material file.
struct TextureMaps {
    std::string color;
    std::string normal;
    std::string roughness;
};

struct MaterialSet {
    std::string name = "unnamed";
    TextureMaps bark;
    TextureMaps foliage;

    // Foliage cards are cut out of an alpha-masked twig photograph: the stem
    // enters the image at (foliagePivotU, 0) and the image is
    // foliageAspect times as wide as it is tall.
    float foliageAspect = 0.85f;
    float foliagePivotU = 0.5f;
    float foliageAlphaCutoff = 0.5f;

    // Meters of bark texture per UV repeat along and around a stem.
    float barkUvLengthScale = 1.0f;
};

// Versioned JSON, schema v1 (parse is strict: unknown keys are errors):
// {
//   "format": "flora-materials",
//   "version": 1,
//   "name": "Oak",
//   "bark":    {"color": "...", "normal": "...", "roughness": "...",
//               "uvLengthScale": 1.0},
//   "foliage": {"color": "...", "normal": "...", "roughness": "...",
//               "aspect": 0.85, "pivotU": 0.5, "alphaCutoff": 0.5}
// }
bool loadMaterialSetFromJson(const std::string& jsonText, MaterialSet& materials,
                             std::string* error = nullptr);

// Loads a material file and rewrites texture paths so they resolve relative to
// the caller's working directory rather than to the file.
bool loadMaterialSetFromFile(const std::string& path, MaterialSet& materials,
                             std::string* error = nullptr);

// Wavefront MTL text for the "bark" and "foliage" groups written by the OBJ
// exporter. texturePathPrefix is prepended to every map path (use it to make
// paths relative to the OBJ's directory).
std::string materialLibraryText(const MaterialSet& materials,
                                const std::string& texturePathPrefix = "");

} // namespace flora

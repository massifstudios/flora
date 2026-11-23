#pragma once

#include <string>
#include <vector>

#include "flora/geometry/foliage_cards.hpp"
#include "flora/growth/parameters.hpp"

namespace flora {

// Texture references for generated geometry. Presentation data, kept out of
// species definitions: a species is simulation biology, a material set is one
// possible look for it (ARCHITECTURE.md §9). Paths are relative to the
// material file.
struct TextureMaps {
    std::string color;
    std::string normal;
    std::string roughness;
    std::string depth;
};

// One photographed twig plus the range of twig it stands for. A twig atlas is
// normally a series shot at different foliage densities -- a fully clothed
// shoot, a thinning one, a nearly bare one -- and which photograph is right for
// a card is a property of the simulated twig, not a coin flip. The matching
// rule lives in TwigCardVariant (foliage_cards.hpp), so the geometry module
// selects without ever seeing a texture path.
struct FoliageVariant {
    TextureMaps maps;
    TwigCardVariant selector;
};

struct MaterialSet {
    std::string name = "unnamed";
    TextureMaps bark;
    // At least one after a successful load; a single-texture look is a
    // one-entry list, so there is no second representation to keep in step.
    std::vector<FoliageVariant> foliage;

    // Foliage cards are cut out of an alpha-masked twig photograph: the stem
    // enters the image at (foliagePivotU, 0) and the image is foliageAspect
    // times as wide as it is tall. Card proportions are shared by every
    // variant: a twig atlas is one photo shoot, so its images crop alike, and
    // per-variant proportions would resize a card according to whichever
    // texture happened to win it.
    float foliageAspect = 0.85f;
    float foliagePivotU = 0.5f;
    float foliageAlphaCutoff = 0.5f;
    // How much twig one card stands for. sizeScale grows the plate around its
    // attachment without changing how the branchlets are cut up, so it is the
    // knob for "the photographed spray covers more of the bough"; maxLength is
    // both the height cap and the stem run per card, so raising it makes cards
    // longer and fewer. Defaults match TwigCardParameters.
    float foliageSizeScale = 1.0f;
    float foliageMinHeight = 0.12f;
    float foliageMaxLength = 0.6f;
    // Sibling twigs emitted per card. The curtain a spruce bough carries is
    // denser than one photographed twig per branchlet run, and this is where
    // that density belongs: growing the extra branchlets in the simulation
    // instead would spend energy and cast shade on wood the same LOD merges
    // straight back into a card. Defaults match TwigCardParameters.
    std::uint32_t foliageInstances = 1;
    float foliageInstanceSpread = 0.5f;
    float foliageInstanceScaleJitter = 0.25f;

    // Meters of bark texture per UV repeat along and around a stem.
    float barkUvLengthScale = 1.0f;
};

// Strict JSON: unknown keys are errors, and the version must be exactly
// kMaterialSchemaVersion (development reset; see CLAUDE.md "Serialization").
//
// {
//   "format": "flora-materials",
//   "version": 3,
//   "name": "Spruce",
//   "bark": {"color": "...", "normal": "...", "roughness": "...",
//            "depth": "...", "uvLengthScale": 1.0},
//   "foliage": {
//     "aspect": 1.0, "pivotU": 0.5, "alphaCutoff": 0.5,
//     "instances": 3, "instanceSpread": 0.5, "instanceScaleJitter": 0.25,
//     "variants": [
//       {"color": "dense.png",  "depth": "dense-depth.png",  "minDensity": 0.9},
//       {"color": "sparse.png", "depth": "sparse-depth.png",
//        "minDensity": 0.4, "maxDensity": 0.9},
//       {"color": "bare.png",   "maxDensity": 0.4}
//     ]
//   }
// }
//
// Every variant needs a "color"; "normal", "roughness" and "depth" are
// optional. Condition keys are minDensity/maxDensity, minClusters/maxClusters,
// minCrownDepth/maxCrownDepth, minBranchFraction/maxBranchFraction, plus
// "weight" to bias the draw between variants whose conditions overlap. A card
// matching no variant falls back to the first entry, so list the default first.
bool loadMaterialSetFromJson(const std::string& jsonText, MaterialSet& materials,
                             std::string* error = nullptr);

// Level-0 card parameters for one plant. Species authority decides what counts
// as a twig and how tightly clusters sit; material authority decides how the
// plate is cut, scaled and fanned. Every tool that draws a plant needs the same
// combination, and getting half of it from one side is how a card ends up
// standing for wood that is also swept.
TwigCardParameters twigCardParameters(const MaterialSet& materials,
                                      const GrowthParameters& growth, std::uint64_t seed);

// Loads a material file and rewrites texture paths so they resolve relative to
// the caller's working directory rather than to the file.
bool loadMaterialSetFromFile(const std::string& path, MaterialSet& materials,
                             std::string* error = nullptr);

// Wavefront MTL text for the groups written by the OBJ exporter: "bark", and
// "foliage" for a single-variant set or "foliage_0".."foliage_n" for a list.
// texturePathPrefix is prepended to every map path (use it to make paths
// relative to the OBJ's directory).
std::string materialLibraryText(const MaterialSet& materials,
                                const std::string& texturePathPrefix = "");

// Group and material name for a foliage variant; matches materialLibraryText.
std::string foliageMaterialName(const MaterialSet& materials, std::size_t variant);

// Condition selectors in material order, for TwigCardParameters::variants.
std::vector<TwigCardVariant> foliageSelectors(const MaterialSet& materials);

} // namespace flora

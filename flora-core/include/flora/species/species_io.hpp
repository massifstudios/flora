#pragma once

#include <string>

#include "flora/species/species.hpp"

namespace flora {

// JSON schema v1 (versioned; see docs in presets/). Angles are degrees in
// JSON, radians in memory. Parsing is strict: unknown keys, bad types,
// malformed distributions, and unsupported versions are errors.
//
// Shape:
// {
//   "format": "flora-species",
//   "version": 1,
//   "name": "...",
//   "parameters": {
//     "<float param>": 0.5                        // shorthand: mean only
//     "<float param>": {"mean": .., "stddev": ..,  // full distribution
//                        "min": .., "max": ..,
//                        "overAge": [[age, mean], ...]},
//     "<integer param>": 3
//   }
// }

bool loadSpeciesFromJson(const std::string& jsonText, SpeciesDefinition& species,
                         std::string* error = nullptr);

std::string saveSpeciesToJson(const SpeciesDefinition& species);

} // namespace flora

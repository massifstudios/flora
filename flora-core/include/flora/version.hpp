#pragma once

#include <cstdint>

namespace flora {

inline constexpr std::uint32_t kVersionMajor = 0;
inline constexpr std::uint32_t kVersionMinor = 1;
inline constexpr std::uint32_t kVersionPatch = 0;

// Bumped whenever a change alters deterministic simulation output for identical
// inputs. Serialized data records this value.
inline constexpr std::uint32_t kSimulationRevision = 5;

} // namespace flora

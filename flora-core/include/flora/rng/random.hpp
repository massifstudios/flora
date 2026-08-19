#pragma once

#include <cmath>
#include <cstdint>

namespace flora {

// Deterministic randomness (ARCHITECTURE.md §8).
//
// Streams are derived from stable identifier tuples — (worldSeed, purpose,
// plantId, entityId, step, ...) — never from global state, time, addresses, or
// thread identity. Two streams with different keys are statistically
// independent; consuming extra values from one stream never shifts another.
// Sequences are pinned by golden-value tests; changing them is a simulation
// revision bump (version.hpp).

namespace detail {

// SplitMix64 finalizer (Steele, Lea, Flood 2014). Bijective 64-bit mix.
constexpr std::uint64_t mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

constexpr std::uint64_t combine(std::uint64_t seed, std::uint64_t id) {
    return mix64(seed ^ (id + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2)));
}

} // namespace detail

// Simulation phases and subsystems draw from separate purposes so unrelated
// systems cannot perturb each other's sequences. Append only; renumbering is a
// simulation revision bump.
enum class RngPurpose : std::uint64_t {
    Test = 0,
    PlantSeed = 1,
    BudFate = 2,
    ShootGrowth = 3,
    Orientation = 4,
    SpeciesVariation = 5,
    ForestDistribution = 6,
};

// Sequential deterministic generator over a derived key (SplitMix64 stream).
class RandomStream {
public:
    explicit constexpr RandomStream(std::uint64_t derivedKey) : state_(derivedKey) {}

    template <typename... Ids>
    static constexpr RandomStream derive(std::uint64_t worldSeed, RngPurpose purpose, Ids... ids) {
        std::uint64_t key = detail::combine(worldSeed, static_cast<std::uint64_t>(purpose));
        ((key = detail::combine(key, static_cast<std::uint64_t>(ids))), ...);
        return RandomStream(key);
    }

    constexpr std::uint64_t nextUInt64() {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t value = state_;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    constexpr std::uint32_t nextUInt32() { return static_cast<std::uint32_t>(nextUInt64() >> 32); }

    // Uniform in [0, 1). 24 explicit mantissa bits; exact float, no rounding to 1.
    constexpr float uniformFloat() {
        return static_cast<float>(nextUInt64() >> 40) * 0x1.0p-24f;
    }

    constexpr float uniformFloat(float minimum, float maximum) {
        return minimum + (maximum - minimum) * uniformFloat();
    }

    // Uniform integer in [0, bound). Fixed-point multiply keeps the draw count
    // constant (no rejection loop), bias is negligible for simulation bounds.
    constexpr std::uint32_t uniformIndex(std::uint32_t bound) {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(nextUInt32()) * static_cast<std::uint64_t>(bound)) >> 32);
    }

    // Box-Muller (cosine branch only: exactly two draws per value, so the
    // stream advance is predictable).
    float normal(float mean, float stddev) {
        constexpr double kTwoPi = 6.283185307179586476925286766559;
        const double u1 = (static_cast<double>(nextUInt64() >> 11) + 1.0) * 0x1.0p-53;
        const double u2 = static_cast<double>(nextUInt64() >> 11) * 0x1.0p-53;
        const double radius = std::sqrt(-2.0 * std::log(u1));
        return mean + stddev * static_cast<float>(radius * std::cos(kTwoPi * u2));
    }

private:
    std::uint64_t state_;
};

} // namespace flora

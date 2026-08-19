#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

#include "flora/rng/random.hpp"

using namespace flora;

TEST_CASE("identical keys produce identical sequences") {
    auto first = RandomStream::derive(99, RngPurpose::BudFate, 12, 5);
    auto second = RandomStream::derive(99, RngPurpose::BudFate, 12, 5);
    for (int i = 0; i < 100; ++i) {
        CHECK(first.nextUInt64() == second.nextUInt64());
    }
}

TEST_CASE("different key components produce different sequences") {
    auto base = RandomStream::derive(99, RngPurpose::BudFate, 12, 5);
    auto otherSeed = RandomStream::derive(100, RngPurpose::BudFate, 12, 5);
    auto otherPurpose = RandomStream::derive(99, RngPurpose::ShootGrowth, 12, 5);
    auto otherEntity = RandomStream::derive(99, RngPurpose::BudFate, 12, 6);

    const std::uint64_t reference = base.nextUInt64();
    CHECK(reference != otherSeed.nextUInt64());
    CHECK(reference != otherPurpose.nextUInt64());
    CHECK(reference != otherEntity.nextUInt64());
}

TEST_CASE("golden values pin the sequence for this simulation revision") {
    // Changing these values is a deterministic-output change and requires a
    // kSimulationRevision bump (see version.hpp).
    auto integers = RandomStream::derive(42, RngPurpose::Test, 7, 3);
    CHECK(integers.nextUInt64() == 0xB44A8AC31CEADD87ull);
    CHECK(integers.nextUInt64() == 0x8E2CCE7DC1A4B6F4ull);
    CHECK(integers.nextUInt64() == 0x236F3B766D5B3E68ull);
    CHECK(integers.nextUInt64() == 0xF27242BAD778F103ull);

    auto floats = RandomStream::derive(42, RngPurpose::Test, 7, 3);
    CHECK(floats.uniformFloat() == doctest::Approx(0.704262376f).epsilon(1e-9));
    CHECK(floats.uniformFloat() == doctest::Approx(0.555371165f).epsilon(1e-9));
    CHECK(floats.uniformFloat() == doctest::Approx(0.138415992f).epsilon(1e-9));
    CHECK(floats.uniformFloat() == doctest::Approx(0.947055936f).epsilon(1e-9));
}

TEST_CASE("uniformFloat stays in [0, 1)") {
    auto stream = RandomStream::derive(7, RngPurpose::Test, 0);
    for (int i = 0; i < 100000; ++i) {
        const float value = stream.uniformFloat();
        CHECK(value >= 0.0f);
        CHECK(value < 1.0f);
    }
}

TEST_CASE("uniformIndex stays in bounds and covers the range") {
    auto stream = RandomStream::derive(7, RngPurpose::Test, 1);
    constexpr std::uint32_t kBound = 7;
    std::uint32_t histogram[kBound] = {};
    for (int i = 0; i < 70000; ++i) {
        const std::uint32_t value = stream.uniformIndex(kBound);
        REQUIRE(value < kBound);
        ++histogram[value];
    }
    for (std::uint32_t count : histogram) {
        CHECK(count > 8000); // expectation 10000 per bucket
    }
}

TEST_CASE("normal distribution has the requested moments") {
    auto stream = RandomStream::derive(1234, RngPurpose::PlantSeed, 0);
    constexpr int kSamples = 200000;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const double value = stream.normal(3.0f, 2.0f);
        sum += value;
        sumSquares += value * value;
    }
    const double mean = sum / kSamples;
    const double variance = sumSquares / kSamples - mean * mean;
    CHECK(mean == doctest::Approx(3.0).epsilon(0.02));
    CHECK(std::sqrt(variance) == doctest::Approx(2.0).epsilon(0.02));
}

TEST_CASE("stream independence: extra draws on one stream do not shift another") {
    auto consumer = RandomStream::derive(55, RngPurpose::Orientation, 1);
    for (int i = 0; i < 1000; ++i) {
        (void)consumer.nextUInt64();
    }
    auto untouched = RandomStream::derive(55, RngPurpose::BudFate, 1);
    auto reference = RandomStream::derive(55, RngPurpose::BudFate, 1);
    CHECK(untouched.nextUInt64() == reference.nextUInt64());
}

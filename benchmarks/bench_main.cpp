#include <cstdio>
#include <vector>

#include "bench.hpp"
#include "flora/rng/random.hpp"
#include "flora/structure/stem_graph.hpp"
#include "flora/version.hpp"

using namespace flora;

namespace {

// Same synthetic shape as the unit tests, scaled up: main axis with one
// lateral + bud per internode. Deterministic, allocation pattern matches
// realistic growth (interleaved axis extension and lateral creation).
StemGraph buildSyntheticTree(std::uint32_t axisLength) {
    StemGraph graph;
    graph.reserve(static_cast<std::size_t>(axisLength) * 2 + 1, axisLength + 1);
    Index tip = graph.addRoot(kUpAxis, 0.5f, 0.05f, 0);
    for (std::uint32_t step = 1; step <= axisLength; ++step) {
        const Index lateral = graph.addInternode(
            tip, AxisRelation::Lateral, Vec3{1.0f, 0.0f, 0.5f}.normalizedOr(kUpAxis), 0.3f, 0.02f, step);
        graph.addBud(lateral, BudType::Apical, kUpAxis, step);
        tip = graph.addInternode(tip, AxisRelation::Continuation, kUpAxis, 0.5f, 0.05f, step);
    }
    return graph;
}

} // namespace

int main() {
    std::printf("flora benchmarks — version %u.%u.%u, simulation revision %u\n", kVersionMajor,
                kVersionMinor, kVersionPatch, kSimulationRevision);

    bench::Registry registry;

    constexpr std::uint32_t kAxisLength = 50000;
    constexpr std::uint64_t kInternodeCount = static_cast<std::uint64_t>(kAxisLength) * 2 + 1;

    registry.run("stem_graph/build_100k_internodes", kInternodeCount, "internode", [&] {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        if (graph.internodeCount() != kInternodeCount) {
            std::printf("unexpected internode count\n");
        }
    });

    {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        std::vector<Vec3> tips;
        registry.run("stem_graph/compute_tip_positions_100k", kInternodeCount, "internode",
                     [&] { graph.computeTipPositions(tips); });
    }

    registry.run("stem_graph/prune_half_and_compact_100k", kInternodeCount, "internode", [&] {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        graph.markSubtreeDead(kAxisLength); // roughly the upper half of the axis
        graph.sweepDeadBuds();
        graph.compact();
    });

    {
        StemGraph graph = buildSyntheticTree(kAxisLength);
        std::string errors;
        registry.run("stem_graph/validate_100k", kInternodeCount, "internode", [&] {
            if (!graph.validate(&errors)) {
                std::printf("validate failed: %s\n", errors.c_str());
            }
        });
    }

    constexpr std::uint64_t kRngDraws = 10'000'000;
    registry.run("rng/next_uint64_10m", kRngDraws, "draw", [&] {
        auto stream = RandomStream::derive(1, RngPurpose::Test, 0);
        std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < kRngDraws; ++i) {
            sink ^= stream.nextUInt64();
        }
        if (sink == 0) {
            std::printf("unlikely\n");
        }
    });

    registry.report();

    const double bytesPerInternode = static_cast<double>(sizeof(Internode));
    const double bytesPerBud = static_cast<double>(sizeof(Bud));
    std::printf("\nmemory: sizeof(Internode) = %.0f B, sizeof(Bud) = %.0f B\n", bytesPerInternode,
                bytesPerBud);
    std::printf("100k-internode tree structure: %.2f MB (+%.2f MB buds)\n",
                bytesPerInternode * static_cast<double>(kInternodeCount) / 1.0e6,
                bytesPerBud * static_cast<double>(kAxisLength + 1) / 1.0e6);
    return 0;
}

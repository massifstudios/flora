#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "flora/rng/random.hpp"
#include "flora/structure/stem_graph.hpp"

using namespace flora;

namespace {

// Deterministic synthetic tree: a main axis with one lateral (plus bud) per
// internode. Shared by tests; the benchmark builds the same shape at scale.
StemGraph buildSampleTree(std::uint32_t axisLength) {
    StemGraph graph;
    Index tip = graph.addRoot(kUpAxis, 0.5f, 0.05f, 0);
    for (std::uint32_t step = 1; step <= axisLength; ++step) {
        const Index lateral = graph.addInternode(
            tip, AxisRelation::Lateral, Vec3{1.0f, 0.0f, 0.5f}.normalizedOr(kUpAxis), 0.3f, 0.02f, step);
        graph.addBud(lateral, BudType::Apical, kUpAxis, step);
        tip = graph.addInternode(tip, AxisRelation::Continuation, kUpAxis, 0.5f, 0.05f, step);
    }
    graph.addBud(tip, BudType::Apical, kUpAxis, axisLength);
    return graph;
}

} // namespace

TEST_CASE("construction maintains structural invariants") {
    StemGraph graph = buildSampleTree(10);
    std::string errors;
    CHECK_MESSAGE(graph.validate(&errors), errors);
    CHECK(graph.internodeCount() == 21); // root + 10 * (lateral + continuation)
    CHECK(graph.budCount() == 11);
}

TEST_CASE("parents are always created before children") {
    StemGraph graph = buildSampleTree(25);
    for (Index i = 0; i < graph.internodeCount(); ++i) {
        const Index parent = graph.internode(i).parent;
        if (parent != kInvalidIndex) {
            CHECK(parent < i);
        }
    }
}

TEST_CASE("lateral children increment branch order, continuations keep it") {
    StemGraph graph;
    const Index root = graph.addRoot(kUpAxis, 1.0f, 0.1f, 0);
    const Index continuation =
        graph.addInternode(root, AxisRelation::Continuation, kUpAxis, 1.0f, 0.1f, 1);
    const Index lateral =
        graph.addInternode(root, AxisRelation::Lateral, Vec3{1, 0, 0}, 0.5f, 0.05f, 1);
    CHECK(graph.internode(continuation).branchOrder == 0);
    CHECK(graph.internode(lateral).branchOrder == 1);
}

TEST_CASE("tip positions accumulate root to tip") {
    StemGraph graph;
    const Index root = graph.addRoot(kUpAxis, 2.0f, 0.1f, 0);
    const Index next = graph.addInternode(root, AxisRelation::Continuation, kUpAxis, 1.5f, 0.1f, 1);
    graph.addInternode(next, AxisRelation::Lateral, Vec3{1, 0, 0}, 1.0f, 0.05f, 2);

    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);
    CHECK(tips[0] == Vec3{0.0f, 0.0f, 2.0f});
    CHECK(tips[1] == Vec3{0.0f, 0.0f, 3.5f});
    CHECK(tips[2] == Vec3{1.0f, 0.0f, 3.5f});
    for (const Vec3& tip : tips) {
        CHECK(tip.isFinite());
    }
}

TEST_CASE("pruning marks the whole subtree and its buds") {
    StemGraph graph = buildSampleTree(10);

    // First continuation internode of the main axis: everything above dies.
    const Index firstContinuation = 2;
    const std::uint32_t marked = graph.markSubtreeDead(firstContinuation);
    CHECK(marked == 19); // all but root and its first lateral
    CHECK(graph.deadInternodeCount() == 19);
    CHECK(graph.sweepDeadBuds() == 10); // every bud except the first lateral's

    std::string errors;
    CHECK_MESSAGE(graph.validate(&errors), errors);
}

TEST_CASE("pruning twice does not double-count") {
    StemGraph graph = buildSampleTree(5);
    const std::uint32_t first = graph.markSubtreeDead(2);
    const std::uint32_t second = graph.markSubtreeDead(2);
    CHECK(second == 0);
    CHECK(graph.deadInternodeCount() == first);
}

TEST_CASE("compaction removes dead records, preserves order, remaps indices") {
    StemGraph graph = buildSampleTree(10);
    graph.markSubtreeDead(2);
    graph.sweepDeadBuds();

    const std::size_t liveExpected = graph.internodeCount() - graph.deadInternodeCount();
    graph.compact();

    CHECK(graph.internodeCount() == liveExpected);
    CHECK(graph.internodeCount() == 2);
    CHECK(graph.budCount() == 1);
    CHECK(graph.deadInternodeCount() == 0);

    // Remaining structure: root (order 0) with its first lateral (order 1),
    // whose bud survived and was remapped.
    CHECK(graph.internode(0).parent == kInvalidIndex);
    CHECK(graph.internode(1).parent == 0);
    CHECK(graph.internode(1).branchOrder == 1);
    CHECK(graph.bud(0).internode == 1);

    std::string errors;
    CHECK_MESSAGE(graph.validate(&errors), errors);
}

TEST_CASE("graph stays usable after compaction") {
    StemGraph graph = buildSampleTree(8);
    graph.markSubtreeDead(4);
    graph.sweepDeadBuds();
    graph.compact();

    const Index tip = static_cast<Index>(graph.internodeCount() - 1);
    const Index added =
        graph.addInternode(tip, AxisRelation::Continuation, kUpAxis, 0.5f, 0.04f, 99);
    graph.addBud(added, BudType::Apical, kUpAxis, 99);

    std::string errors;
    CHECK_MESSAGE(graph.validate(&errors), errors);
}

TEST_CASE("sibling order survives compaction") {
    StemGraph graph;
    const Index root = graph.addRoot(kUpAxis, 1.0f, 0.1f, 0);
    const Index childA = graph.addInternode(root, AxisRelation::Lateral, Vec3{1, 0, 0}, 0.5f, 0.02f, 1);
    const Index childB = graph.addInternode(root, AxisRelation::Lateral, Vec3{0, 1, 0}, 0.5f, 0.02f, 1);
    const Index childC = graph.addInternode(root, AxisRelation::Lateral, Vec3{-1, 0, 0}, 0.5f, 0.02f, 1);
    (void)childA;
    (void)childC;

    graph.markSubtreeDead(childB);
    graph.sweepDeadBuds();
    graph.compact();

    // Children were index-ordered before, remain index-ordered after.
    const Index first = graph.internode(0).firstChild;
    const Index second = graph.internode(first).nextSibling;
    CHECK(first == 1);
    CHECK(second == 2);
    CHECK(graph.internode(second).nextSibling == kInvalidIndex);
    CHECK(graph.internode(first).direction.x == doctest::Approx(1.0f));
    CHECK(graph.internode(second).direction.x == doctest::Approx(-1.0f));
}

TEST_CASE("seeded construction is exactly reproducible") {
    auto build = [](std::uint64_t seed) {
        StemGraph graph;
        auto stream = RandomStream::derive(seed, RngPurpose::Test, 0);
        Index tip = graph.addRoot(kUpAxis, 0.5f, 0.05f, 0);
        for (std::uint32_t step = 1; step <= 200; ++step) {
            const Vec3 direction =
                Vec3{stream.uniformFloat(-1.0f, 1.0f), stream.uniformFloat(-1.0f, 1.0f),
                     stream.uniformFloat(0.2f, 1.0f)}
                    .normalizedOr(kUpAxis);
            if (stream.uniformFloat() < 0.3f) {
                graph.addInternode(tip, AxisRelation::Lateral, direction, 0.3f, 0.02f, step);
            }
            tip = graph.addInternode(tip, AxisRelation::Continuation, direction, 0.5f, 0.05f, step);
        }
        return graph;
    };

    const StemGraph first = build(2024);
    const StemGraph second = build(2024);
    const StemGraph different = build(2025);

    REQUIRE(first.internodeCount() == second.internodeCount());
    for (Index i = 0; i < first.internodeCount(); ++i) {
        const Internode& a = first.internode(i);
        const Internode& b = second.internode(i);
        CHECK(a.parent == b.parent);
        CHECK(a.direction == b.direction);
        CHECK(a.length == b.length);
        CHECK(a.branchOrder == b.branchOrder);
    }
    CHECK(first.internodeCount() != different.internodeCount());
}

TEST_CASE("validate reports violations") {
    StemGraph graph = buildSampleTree(3);
    graph.internode(1).length = -1.0f;
    std::string errors;
    CHECK_FALSE(graph.validate(&errors));
    CHECK(errors.find("length") != std::string::npos);
}

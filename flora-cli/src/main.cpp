#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "flora/rng/random.hpp"
#include "flora/structure/stem_graph.hpp"
#include "flora/version.hpp"

using namespace flora;

namespace {

int printUsage() {
    std::printf("flora %u.%u.%u (simulation revision %u)\n", kVersionMajor, kVersionMinor,
                kVersionPatch, kSimulationRevision);
    std::printf("usage:\n");
    std::printf("  flora version              print version information\n");
    std::printf("  flora diag [seed]          build a diagnostic structure and print stats\n");
    return 1;
}

// Placeholder structure generator until M2 growth exists: random-walk axis
// with probabilistic laterals, exercising graph, RNG, and invariants end to
// end from the command line.
int runDiagnostic(std::uint64_t seed) {
    StemGraph graph;
    auto stream = RandomStream::derive(seed, RngPurpose::Test, 0);

    Index tip = graph.addRoot(kUpAxis, 0.5f, 0.05f, 0);
    for (std::uint32_t step = 1; step <= 5000; ++step) {
        const Vec3 direction = Vec3{stream.uniformFloat(-0.4f, 0.4f), stream.uniformFloat(-0.4f, 0.4f),
                                    stream.uniformFloat(0.6f, 1.0f)}
                                   .normalizedOr(kUpAxis);
        if (stream.uniformFloat() < 0.35f) {
            const Index lateral =
                graph.addInternode(tip, AxisRelation::Lateral, direction, 0.3f, 0.02f, step);
            graph.addBud(lateral, BudType::Apical, direction, step);
        }
        tip = graph.addInternode(tip, AxisRelation::Continuation, direction, 0.5f, 0.05f, step);
    }

    std::string errors;
    const bool valid = graph.validate(&errors);

    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);
    Vec3 highest{};
    for (const Vec3& position : tips) {
        if (position.z > highest.z) {
            highest = position;
        }
    }

    std::printf("seed:        %llu\n", static_cast<unsigned long long>(seed));
    std::printf("internodes:  %zu\n", graph.internodeCount());
    std::printf("buds:        %zu\n", graph.budCount());
    std::printf("apex height: %.2f m\n", static_cast<double>(highest.z));
    std::printf("invariants:  %s\n", valid ? "ok" : errors.c_str());
    return valid ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return printUsage();
    }
    if (std::strcmp(argv[1], "version") == 0) {
        std::printf("flora %u.%u.%u (simulation revision %u)\n", kVersionMajor, kVersionMinor,
                    kVersionPatch, kSimulationRevision);
        return 0;
    }
    if (std::strcmp(argv[1], "diag") == 0) {
        const std::uint64_t seed = argc > 2 ? std::stoull(argv[2]) : 42;
        return runDiagnostic(seed);
    }
    return printUsage();
}

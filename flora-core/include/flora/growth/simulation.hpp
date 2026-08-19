#pragma once

#include <cstdint>
#include <vector>

#include "flora/growth/parameters.hpp"
#include "flora/structure/stem_graph.hpp"

namespace flora {

// A simulated plant: identity + structural state. Forest identity (plant id,
// position) arrives with the forest milestones; (seed, ageSteps, graph) is the
// deterministic reproduction key at this stage.
struct Plant {
    std::uint64_t seed = 0;
    std::uint32_t ageSteps = 0;
    StemGraph graph;
};

// Creates the initial structure: one root internode with an apical bud.
void plantSeedling(Plant& plant, const GrowthParameters& parameters);

// Scratch buffers reused across steps and plants so the growth loop performs
// no hidden per-step allocations once warm (ARCHITECTURE.md §5.1).
class GrowthContext {
public:
    friend void growStep(Plant&, const GrowthParameters&, const ConstantEnvironment&,
                         GrowthContext&);

private:
    std::vector<std::uint32_t> budOffsets_; // CSR: buds grouped by internode
    std::vector<Index> budIndices_;
    std::vector<float> energy_;      // per internode, phase 2
    std::vector<float> resource_;    // per internode, phase 3
    std::vector<float> budResource_; // per bud, phase 3
};

// One fixed simulation step in the phase order of ARCHITECTURE.md §6
// (phases 1-5 and 9; tropisms, thickening, and shedding arrive in M5/M6).
void growStep(Plant& plant, const GrowthParameters& parameters,
              const ConstantEnvironment& environment, GrowthContext& context);

void grow(Plant& plant, const GrowthParameters& parameters, const ConstantEnvironment& environment,
          std::uint32_t steps, GrowthContext& context);

} // namespace flora

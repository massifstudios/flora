#pragma once

#include <cstdint>
#include <vector>

#include "flora/environment/environment.hpp"
#include "flora/environment/shadow_grid.hpp"
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
    friend void growStep(Plant&, const GrowthParameters&, const Environment&, GrowthContext&);

    // Per-bud light of the last executed step (indexed like the bud array at
    // step start). Read-only diagnostics for tools and tests.
    const std::vector<float>& lastBudLight() const { return budLight_; }

private:
    std::vector<std::uint32_t> budOffsets_; // CSR: buds grouped by internode
    std::vector<Index> budIndices_;
    std::vector<Vec3> tipPositions_;  // per internode, phase 1
    std::vector<Vec3> casterPositions_; // leafy tips shading the voxel field
    std::vector<float> budLight_;     // per bud, phase 1
    std::vector<Vec3> budLightDirection_; // per bud, phase 1
    std::vector<float> energy_;      // per internode, phase 2
    std::vector<float> woodVolume_;  // per internode subtree, phase 2
    std::vector<float> resource_;    // per internode, phase 4
    std::vector<float> budResource_; // per bud, phase 4
    std::vector<std::pair<float, Index>> shedCandidates_; // (viability, index), phase 3
    ShadowVoxelGrid shadowGrid_;     // phase 1 self-shading field
};

// One fixed simulation step in the phase order of ARCHITECTURE.md §6.
void growStep(Plant& plant, const GrowthParameters& parameters, const Environment& environment,
              GrowthContext& context);

void grow(Plant& plant, const GrowthParameters& parameters, const Environment& environment,
          std::uint32_t steps, GrowthContext& context);

} // namespace flora

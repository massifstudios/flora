#pragma once

#include <cstdint>

#include "flora/geometry/mesh.hpp"
#include "flora/structure/stem_graph.hpp"

namespace flora {

enum class RadiusSource : std::uint8_t {
    // Radii as stored in the structure (authoritative once M5 secondary
    // thickening simulates them).
    Stored,
    // Display heuristic until M5: radius from downstream metamer count via a
    // pipe-model relation r = storedRadius * count^(1/exponent). Derived from
    // topology only; no state is written back to the structure.
    PipeModelDisplay,
};

struct BranchMeshParameters {
    std::uint32_t radialSegments = 8;       // ring segments at decimationRadiusMax and above
    std::uint32_t minimumRadialSegments = 3;
    float decimationRadiusMin = 0.015f;     // radius at or below: minimum segments
    float decimationRadiusMax = 0.12f;      // radius at or above: full segments
    float tipTaper = 0.25f;                 // final ring radius factor at an axis tip
    float uvLengthScale = 1.0f;             // meters of axis length per V repeat
    RadiusSource radiusSource = RadiusSource::Stored;
    float displayRadiusExponent = 2.8f;
};

// Sweeps every live axis of the graph into a tube: one ring per internode
// boundary with parallel-transport frames (no twist), radius-driven radial
// decimation per axis, seam-duplicated vertices for wrapped UVs, and a fan cap
// at each axis tip. Deterministic: identical graph + parameters produce an
// identical mesh. Output mesh is cleared first.
//
// Complexity: O(internodes × radialSegments); one topology pass, one sweep.
void generateBranchMesh(const StemGraph& graph, const BranchMeshParameters& parameters, Mesh& mesh);

} // namespace flora

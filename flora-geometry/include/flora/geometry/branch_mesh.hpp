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
    std::uint32_t radialSegments = 12;      // ring segments at decimationRadiusMax and above
    std::uint32_t minimumRadialSegments = 3;
    float decimationRadiusMin = 0.015f;     // radius at or below: minimum segments
    float decimationRadiusMax = 0.30f;      // radius at or above: full segments
    float tipTaper = 0.25f;                 // final ring radius factor at an axis tip
    float uvLengthScale = 1.0f;             // meters of axis length per V repeat
    // Meters of girth per U repeat: bark keeps its real-world scale around a
    // stem instead of one texture wrapping a twig and a 2.5 m trunk alike.
    // 0 falls back to a single wrap per ring.
    float uvGirthScale = 1.0f;
    RadiusSource radiusSource = RadiusSource::Stored;
    float displayRadiusExponent = 2.8f;
    // Stems thinner than this (and everything beyond them) are not swept.
    // Set it to TwigCardParameters::maxBearingRadius when foliage cards stand
    // in for the twigs: the card texture already contains that wood. 0 meshes
    // every live stem.
    float cullBelowRadius = 0.0f;
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

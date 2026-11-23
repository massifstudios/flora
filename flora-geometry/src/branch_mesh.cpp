#include "flora/geometry/branch_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "flora/math/rotation.hpp"

namespace flora {

namespace {

constexpr float kTwoPi = 6.2831853f;

std::uint32_t segmentsForRadius(const BranchMeshParameters& parameters, float radius,
                                std::uint16_t branchOrder) {
    const bool bole = branchOrder == 0;
    const float minimum = parameters.decimationRadiusMin;
    const float maximum =
        bole ? parameters.decimationRadiusMax : parameters.branchDecimationRadiusMax;
    float blend = 1.0f;
    if (maximum > minimum) {
        blend = std::clamp((radius - minimum) / (maximum - minimum), 0.0f, 1.0f);
    }
    const float low = static_cast<float>(bole ? parameters.minimumRadialSegments
                                              : parameters.minimumBranchRadialSegments);
    const float high =
        static_cast<float>(bole ? parameters.radialSegments : parameters.branchRadialSegments);
    const auto segments = static_cast<std::uint32_t>(std::lround(low + (high - low) * blend));
    return std::max(segments, 3u);
}

// Bridges two rings that need not share a segment count. Both rims are walked
// in normalized angle and whichever is behind advances, so the seam comes out
// as a fan of triangles instead of a quad strip. Rings carry a duplicated seam
// vertex, so both walks finish exactly at 1 and the ends always meet. With
// equal counts this emits the same two triangles per segment as a quad strip.
void stitchRings(Mesh& mesh, std::uint32_t lowerStart, std::uint32_t lowerSegments,
                 std::uint32_t upperStart, std::uint32_t upperSegments) {
    std::uint32_t lower = 0;
    std::uint32_t upper = 0;
    while (lower < lowerSegments || upper < upperSegments) {
        const bool advanceLower =
            upper == upperSegments ||
            (lower < lowerSegments &&
             static_cast<float>(lower + 1) * static_cast<float>(upperSegments) <=
                 static_cast<float>(upper + 1) * static_cast<float>(lowerSegments));
        if (advanceLower) {
            mesh.indices.push_back(lowerStart + lower);
            mesh.indices.push_back(lowerStart + lower + 1);
            mesh.indices.push_back(upperStart + upper);
            ++lower;
        } else {
            mesh.indices.push_back(lowerStart + lower);
            mesh.indices.push_back(upperStart + upper + 1);
            mesh.indices.push_back(upperStart + upper);
            ++upper;
        }
    }
}

} // namespace

void generateBranchMesh(const StemGraph& graph, const BranchMeshParameters& parameters, Mesh& mesh) {
    mesh.clear();
    const std::size_t count = graph.internodeCount();
    if (count == 0) {
        return;
    }

    std::vector<Vec3> tips;
    graph.computeTipPositions(tips);

    // Culled stems drop their whole subtree: a twig card stands in for the
    // branchlets beyond the cut as well as for the leaves on them. Retained
    // stubs are wood the tree still carries, so they mesh like live stems;
    // other deadwood is on its way out and is not drawn.
    std::vector<char> meshed(count, 0);
    for (Index i = 0; i < static_cast<Index>(count); ++i) {
        const Internode& node = graph.internode(i);
        const bool parentMeshed = node.parent == kInvalidIndex || meshed[node.parent] != 0;
        // The radius cull exists because a twig card stands in for the wood it
        // removes. Nothing stands in for a snag, so stubs mesh at any girth.
        const bool standing =
            node.stub || (node.alive && node.radius >= parameters.cullBelowRadius);
        meshed[i] = (standing && parentMeshed) ? 1 : 0;
    }

    // First live continuation child per internode; axes start where the parent
    // link is absent or continues through a different child.
    std::vector<Index> continuation(count, kInvalidIndex);
    for (Index i = 0; i < static_cast<Index>(count); ++i) {
        const Internode& node = graph.internode(i);
        if (meshed[i] == 0) {
            continue;
        }
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = graph.internode(child).nextSibling) {
            const Internode& candidate = graph.internode(child);
            if (meshed[child] != 0 && candidate.branchOrder == node.branchOrder) {
                continuation[i] = child;
                break;
            }
        }
    }

    std::vector<float> radius(count);
    for (Index i = 0; i < static_cast<Index>(count); ++i) {
        radius[i] = graph.internode(i).radius;
    }
    if (parameters.radiusSource == RadiusSource::PipeModelDisplay) {
        // Downstream metamer count in one descending pass (children have
        // higher indices), then the pipe-model display relation.
        std::vector<std::uint32_t> downstream(count, 0);
        for (Index i = 0; i < static_cast<Index>(count); ++i) {
            const Internode& node = graph.internode(i);
            downstream[i] = (node.alive || node.stub) ? 1u : 0u;
        }
        for (std::size_t i = count; i-- > 0;) {
            const Internode& node = graph.internode(static_cast<Index>(i));
            if ((node.alive || node.stub) && node.parent != kInvalidIndex) {
                downstream[node.parent] += downstream[i];
            }
        }
        const float inverseExponent = 1.0f / parameters.displayRadiusExponent;
        for (Index i = 0; i < static_cast<Index>(count); ++i) {
            radius[i] = radius[i] * std::pow(static_cast<float>(downstream[i]), inverseExponent);
        }
    }

    std::vector<Index> axis;
    axis.reserve(64);

    for (Index start = 0; start < static_cast<Index>(count); ++start) {
        const Internode& startNode = graph.internode(start);
        if (meshed[start] == 0) {
            continue;
        }
        if (startNode.parent != kInvalidIndex && continuation[startNode.parent] == start) {
            continue; // interior of another axis
        }

        axis.clear();
        for (Index node = start; node != kInvalidIndex; node = continuation[node]) {
            axis.push_back(node);
        }
        const std::size_t axisLength = axis.size();

        const std::uint16_t axisOrder = graph.internode(axis[0]).branchOrder;

        Vec3 direction = graph.internode(axis[0]).direction.normalizedOr(kUpAxis);
        Vec3 frameNormal = perpendicularTo(direction);
        float vCoordinate = 0.0f;
        std::uint32_t previousRingStart = 0;
        std::uint32_t previousSegments = 0;
        std::uint32_t segments = 0;

        for (std::size_t ring = 0; ring <= axisLength; ++ring) {
            const Index node = axis[std::min(ring, axisLength - 1)];
            const Internode& internode = graph.internode(node);

            if (ring > 0) {
                const Vec3 nextDirection = internode.direction.normalizedOr(direction);
                const Vec3 rotationAxis = direction.cross(nextDirection);
                const float sine = rotationAxis.length();
                if (sine > 1.0e-6f) {
                    const float angle = std::atan2(sine, direction.dot(nextDirection));
                    frameNormal = rotateAroundAxis(frameNormal, rotationAxis * (1.0f / sine), angle);
                }
                direction = nextDirection;
                vCoordinate +=
                    graph.internode(axis[ring - 1]).length / parameters.uvLengthScale;
            }
            frameNormal = (frameNormal - direction * frameNormal.dot(direction))
                              .normalizedOr(perpendicularTo(direction));
            const Vec3 frameBinormal = direction.cross(frameNormal);

            const Vec3 center = ring == 0 ? (internode.parent == kInvalidIndex
                                                 ? Vec3{}
                                                 : tips[internode.parent])
                                          : tips[axis[ring - 1]];
            const float ringRadius = ring < axisLength
                                         ? radius[axis[ring]]
                                         : radius[axis[axisLength - 1]] * parameters.tipTaper;

            const float uRepeats = parameters.uvGirthScale > 0.0f
                                       ? kTwoPi * ringRadius / parameters.uvGirthScale
                                       : 1.0f;

            segments = segmentsForRadius(parameters, ringRadius, axisOrder);
            const std::uint32_t ringStart = static_cast<std::uint32_t>(mesh.positions.size());
            for (std::uint32_t s = 0; s <= segments; ++s) {
                const float around = static_cast<float>(s) / static_cast<float>(segments);
                const float angle = kTwoPi * around;
                const Vec3 radial =
                    frameNormal * std::cos(angle) + frameBinormal * std::sin(angle);
                mesh.positions.push_back(center + radial * ringRadius);
                mesh.normals.push_back(radial);
                mesh.uvs.push_back({around * uRepeats, vCoordinate});
            }

            if (ring > 0) {
                stitchRings(mesh, previousRingStart, previousSegments, ringStart, segments);
            }
            previousRingStart = ringStart;
            previousSegments = segments;
        }

        // Fan cap at the axis tip.
        const std::uint32_t tipVertex = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.push_back(tips[axis[axisLength - 1]]);
        mesh.normals.push_back(direction);
        mesh.uvs.push_back({0.5f, vCoordinate});
        for (std::uint32_t s = 0; s < segments; ++s) {
            mesh.indices.push_back(previousRingStart + s);
            mesh.indices.push_back(previousRingStart + s + 1);
            mesh.indices.push_back(tipVertex);
        }
    }

}

} // namespace flora

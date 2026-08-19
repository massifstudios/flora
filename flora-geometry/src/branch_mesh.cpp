#include "flora/geometry/branch_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "flora/math/rotation.hpp"

namespace flora {

namespace {

constexpr float kTwoPi = 6.2831853f;

std::uint32_t segmentsForRadius(const BranchMeshParameters& parameters, float radius) {
    const float minimum = parameters.decimationRadiusMin;
    const float maximum = parameters.decimationRadiusMax;
    float blend = 1.0f;
    if (maximum > minimum) {
        blend = std::clamp((radius - minimum) / (maximum - minimum), 0.0f, 1.0f);
    }
    const float low = static_cast<float>(parameters.minimumRadialSegments);
    const float high = static_cast<float>(parameters.radialSegments);
    const auto segments = static_cast<std::uint32_t>(std::lround(low + (high - low) * blend));
    return std::max(segments, 3u);
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
    // branchlets beyond the cut as well as for the leaves on them.
    std::vector<char> meshed(count, 0);
    for (Index i = 0; i < static_cast<Index>(count); ++i) {
        const Internode& node = graph.internode(i);
        const bool parentMeshed = node.parent == kInvalidIndex || meshed[node.parent] != 0;
        meshed[i] = (node.alive && parentMeshed && node.radius >= parameters.cullBelowRadius) ? 1 : 0;
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
            downstream[i] = graph.internode(i).alive ? 1u : 0u;
        }
        for (std::size_t i = count; i-- > 0;) {
            const Internode& node = graph.internode(static_cast<Index>(i));
            if (node.alive && node.parent != kInvalidIndex) {
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

        const std::uint32_t segments = segmentsForRadius(parameters, radius[axis[0]]);

        Vec3 direction = graph.internode(axis[0]).direction.normalizedOr(kUpAxis);
        Vec3 frameNormal = perpendicularTo(direction);
        float vCoordinate = 0.0f;
        std::uint32_t previousRingStart = 0;

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
                for (std::uint32_t s = 0; s < segments; ++s) {
                    const std::uint32_t lower0 = previousRingStart + s;
                    const std::uint32_t lower1 = previousRingStart + s + 1;
                    const std::uint32_t upper0 = ringStart + s;
                    const std::uint32_t upper1 = ringStart + s + 1;
                    mesh.indices.push_back(lower0);
                    mesh.indices.push_back(lower1);
                    mesh.indices.push_back(upper1);
                    mesh.indices.push_back(lower0);
                    mesh.indices.push_back(upper1);
                    mesh.indices.push_back(upper0);
                }
            }
            previousRingStart = ringStart;
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

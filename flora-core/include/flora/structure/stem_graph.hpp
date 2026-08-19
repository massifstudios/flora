#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "flora/math/vec3.hpp"

namespace flora {

// Structural plant representation (ARCHITECTURE.md §5.1).
//
// A plant is flat arrays of internodes (metamer segments) and buds addressed
// by stable 32-bit indices. Geometry is derived; only simulation state lives
// here.
//
// Ordering invariants (relied on for determinism and cheap validation):
//  - parent index < child index (parents are created first), so the graph is
//    acyclic by construction and index order is a valid root-to-tip order;
//  - sibling lists are ordered by index (children appended in creation order);
//  - compaction preserves relative order, so both properties survive it.

using Index = std::uint32_t;
inline constexpr Index kInvalidIndex = 0xFFFFFFFFu;

enum class AxisRelation : std::uint8_t {
    Continuation, // same axis as parent: keeps the parent's branch order
    Lateral,      // new axis: branch order = parent order + 1
};

enum class BudType : std::uint8_t { Apical, Lateral };
enum class BudState : std::uint8_t { Dormant, Active, Dead };

struct Internode {
    Index parent = kInvalidIndex;
    Index firstChild = kInvalidIndex;
    Index nextSibling = kInvalidIndex;
    Vec3 direction = kUpAxis; // unit growth direction, base to tip
    float length = 0.0f;      // meters
    float radius = 0.0f;      // meters, at base
    std::uint32_t creationStep = 0;
    std::uint32_t deathStep = 0; // step markSubtreeDead ran; meaningful when !alive
    std::uint16_t branchOrder = 0;
    bool alive = true;
};

struct Bud {
    Index internode = kInvalidIndex; // borne at the distal end of this internode
    Vec3 direction = kUpAxis;
    BudType type = BudType::Apical;
    BudState state = BudState::Dormant;
    // Step of creation or of the meristem's last shoot growth (moving a bud to
    // a new tip renews it); dormancy age is measured against this.
    std::uint32_t creationStep = 0;
    float rollAngle = 0.0f; // phyllotaxis spiral state of this meristem, radians
};

class StemGraph {
public:
    // Construction ------------------------------------------------------------

    Index addRoot(const Vec3& direction, float length, float radius, std::uint32_t creationStep);

    Index addInternode(Index parent, AxisRelation relation, const Vec3& direction, float length,
                       float radius, std::uint32_t creationStep);

    Index addBud(Index internode, BudType type, const Vec3& direction, std::uint32_t creationStep);

    void reserve(std::size_t internodeCount, std::size_t budCount);

    // Pruning -----------------------------------------------------------------

    // Marks the internode and its whole subtree dead, recording deathStep.
    // Returns the number of internodes newly marked. Buds are not touched;
    // call sweepDeadBuds() once after a batch of prunes (single O(buds) pass
    // instead of one per prune).
    std::uint32_t markSubtreeDead(Index internode, std::uint32_t deathStep = 0);

    // Marks buds attached to dead internodes dead. Returns the number swept.
    // Required before validate() after pruning.
    std::uint32_t sweepDeadBuds();

    // Removes dead internodes and dead/orphaned buds, preserving relative
    // order and remapping all indices. Invalidates outstanding indices.
    void compact();

    // Like compact(), but keeps deadwood younger than decayDelaySteps
    // (currentStep - deathStep < decayDelaySteps), so shed branches linger as
    // structure before disappearing.
    void removeDecayed(std::uint32_t currentStep, std::uint32_t decayDelaySteps);

    std::uint32_t deadInternodeCount() const { return deadInternodes_; }

    // Access ------------------------------------------------------------------

    const std::vector<Internode>& internodes() const { return internodes_; }
    const std::vector<Bud>& buds() const { return buds_; }
    Internode& internode(Index index) { return internodes_[index]; }
    const Internode& internode(Index index) const { return internodes_[index]; }
    Bud& bud(Index index) { return buds_[index]; }
    const Bud& bud(Index index) const { return buds_[index]; }
    std::size_t internodeCount() const { return internodes_.size(); }
    std::size_t budCount() const { return buds_.size(); }

    // Derived state -----------------------------------------------------------

    // Fills tip positions (plant-local, roots grow from origin base). Output is
    // indexed like internodes(). Single root-to-tip pass; no allocation beyond
    // resizing the caller's buffer.
    void computeTipPositions(std::vector<Vec3>& tipPositions) const;

    // Invariants --------------------------------------------------------------

    // Returns true when all structural invariants hold; appends a description
    // of each violation to errors when provided.
    bool validate(std::string* errors = nullptr) const;

private:
    void appendChild(Index parent, Index child);

    template <typename KeepPredicate>
    void compactKeeping(KeepPredicate&& keep); // defined in stem_graph.cpp

    std::vector<Internode> internodes_;
    std::vector<Bud> buds_;
    std::vector<Index> scratchStack_; // reused by markSubtreeDead; no per-call allocation
    std::uint32_t deadInternodes_ = 0;
};

} // namespace flora

#include "flora/structure/stem_graph.hpp"

#include <cassert>
#include <cmath>

namespace flora {

Index StemGraph::addRoot(const Vec3& direction, float length, float radius,
                         std::uint32_t creationStep) {
    Internode node;
    node.direction = direction;
    node.length = length;
    node.radius = radius;
    node.creationStep = creationStep;
    node.branchOrder = 0;
    internodes_.push_back(node);
    return static_cast<Index>(internodes_.size() - 1);
}

Index StemGraph::addInternode(Index parent, AxisRelation relation, const Vec3& direction,
                              float length, float radius, std::uint32_t creationStep) {
    assert(parent < internodes_.size());
    assert(internodes_[parent].alive);

    Internode node;
    node.parent = parent;
    node.direction = direction;
    node.length = length;
    node.radius = radius;
    node.creationStep = creationStep;
    const std::uint16_t parentOrder = internodes_[parent].branchOrder;
    node.branchOrder =
        relation == AxisRelation::Lateral ? static_cast<std::uint16_t>(parentOrder + 1) : parentOrder;

    internodes_.push_back(node);
    const Index child = static_cast<Index>(internodes_.size() - 1);
    appendChild(parent, child);
    return child;
}

void StemGraph::appendChild(Index parent, Index child) {
    Index* link = &internodes_[parent].firstChild;
    while (*link != kInvalidIndex) {
        link = &internodes_[*link].nextSibling;
    }
    *link = child;
}

Index StemGraph::addBud(Index internode, BudType type, const Vec3& direction,
                        std::uint32_t creationStep) {
    assert(internode < internodes_.size());
    assert(internodes_[internode].alive);

    Bud bud;
    bud.internode = internode;
    bud.type = type;
    bud.direction = direction;
    bud.state = BudState::Dormant;
    bud.creationStep = creationStep;
    buds_.push_back(bud);
    return static_cast<Index>(buds_.size() - 1);
}

void StemGraph::reserve(std::size_t internodeCount, std::size_t budCount) {
    internodes_.reserve(internodeCount);
    buds_.reserve(budCount);
}

std::uint32_t StemGraph::markSubtreeDead(Index internode, std::uint32_t deathStep) {
    assert(internode < internodes_.size());

    std::uint32_t marked = 0;
    scratchStack_.clear();
    scratchStack_.push_back(internode);
    while (!scratchStack_.empty()) {
        const Index current = scratchStack_.back();
        scratchStack_.pop_back();
        Internode& node = internodes_[current];
        if (node.alive) {
            node.alive = false;
            node.deathStep = deathStep;
            ++marked;
        }
        for (Index child = node.firstChild; child != kInvalidIndex;
             child = internodes_[child].nextSibling) {
            scratchStack_.push_back(child);
        }
    }
    deadInternodes_ += marked;
    return marked;
}

std::uint32_t StemGraph::sweepDeadBuds() {
    std::uint32_t swept = 0;
    for (Bud& bud : buds_) {
        if (bud.state != BudState::Dead && !internodes_[bud.internode].alive) {
            bud.state = BudState::Dead;
            ++swept;
        }
    }
    return swept;
}

void StemGraph::compact() {
    compactKeeping([](const Internode& node) { return node.alive; });
}

void StemGraph::removeDecayed(std::uint32_t currentStep, std::uint32_t decayDelaySteps) {
    compactKeeping([&](const Internode& node) {
        return node.alive || currentStep - node.deathStep < decayDelaySteps;
    });
}

template <typename KeepPredicate>
void StemGraph::compactKeeping(KeepPredicate&& keep) {
    // Order-preserving compaction: remap[i] is the new index of each kept
    // internode i. Because relative order is preserved, parent < child and
    // index-ordered sibling lists survive; child links are rebuilt in index
    // order.
    std::vector<Index> remap(internodes_.size(), kInvalidIndex);
    std::size_t keptCount = 0;
    for (std::size_t i = 0; i < internodes_.size(); ++i) {
        if (keep(internodes_[i])) {
            remap[i] = static_cast<Index>(keptCount);
            ++keptCount;
        }
    }

    std::vector<Internode> liveInternodes;
    liveInternodes.reserve(keptCount);
    for (std::size_t i = 0; i < internodes_.size(); ++i) {
        if (remap[i] == kInvalidIndex) {
            continue;
        }
        Internode node = internodes_[i];
        // A kept node's parent is always kept: children die no later than the
        // subtree root that killed them, so they never outlast it.
        assert(node.parent == kInvalidIndex || remap[node.parent] != kInvalidIndex);
        node.parent = node.parent == kInvalidIndex ? kInvalidIndex : remap[node.parent];
        node.firstChild = kInvalidIndex;
        node.nextSibling = kInvalidIndex;
        liveInternodes.push_back(node);
    }
    for (Index i = 0; i < static_cast<Index>(liveInternodes.size()); ++i) {
        const Index parent = liveInternodes[i].parent;
        if (parent == kInvalidIndex) {
            continue;
        }
        Index* link = &liveInternodes[parent].firstChild;
        while (*link != kInvalidIndex) {
            link = &liveInternodes[*link].nextSibling;
        }
        *link = i;
    }

    std::vector<Bud> liveBuds;
    liveBuds.reserve(buds_.size());
    for (const Bud& bud : buds_) {
        if (bud.state == BudState::Dead || remap[bud.internode] == kInvalidIndex) {
            continue;
        }
        Bud kept = bud;
        kept.internode = remap[bud.internode];
        liveBuds.push_back(kept);
    }

    internodes_ = std::move(liveInternodes);
    buds_ = std::move(liveBuds);
    deadInternodes_ = 0;
    for (const Internode& node : internodes_) {
        if (!node.alive) {
            ++deadInternodes_;
        }
    }
}

void StemGraph::computeTipPositions(std::vector<Vec3>& tipPositions) const {
    tipPositions.resize(internodes_.size());
    for (std::size_t i = 0; i < internodes_.size(); ++i) {
        const Internode& node = internodes_[i];
        const Vec3 base = node.parent == kInvalidIndex ? Vec3{} : tipPositions[node.parent];
        tipPositions[i] = base + node.direction * node.length;
    }
}

namespace {

void appendError(std::string* errors, bool& valid, const char* message) {
    valid = false;
    if (errors != nullptr) {
        errors->append(message);
        errors->push_back('\n');
    }
}

} // namespace

bool StemGraph::validate(std::string* errors) const {
    bool valid = true;
    const Index count = static_cast<Index>(internodes_.size());

    std::uint32_t deadSeen = 0;
    for (Index i = 0; i < count; ++i) {
        const Internode& node = internodes_[i];
        if (!node.alive) {
            ++deadSeen;
        }
        if (node.parent != kInvalidIndex) {
            if (node.parent >= i) {
                appendError(errors, valid, "parent index must be smaller than child index");
            } else {
                const Internode& parent = internodes_[node.parent];
                if (!parent.alive && node.alive) {
                    appendError(errors, valid, "live internode has dead parent");
                }
                const std::uint16_t parentOrder = parent.branchOrder;
                if (node.branchOrder != parentOrder && node.branchOrder != parentOrder + 1) {
                    appendError(errors, valid, "branch order must equal parent order or parent order + 1");
                }
            }
        } else if (node.branchOrder != 0) {
            appendError(errors, valid, "root internode must have branch order 0");
        }
        if (node.firstChild != kInvalidIndex && node.firstChild <= i) {
            appendError(errors, valid, "child index must be greater than parent index");
        }
        if (node.nextSibling != kInvalidIndex && node.nextSibling >= count) {
            appendError(errors, valid, "sibling index out of range");
        }
        if (!(node.length >= 0.0f) || !std::isfinite(node.length)) {
            appendError(errors, valid, "length must be finite and non-negative");
        }
        if (!(node.radius >= 0.0f) || !std::isfinite(node.radius)) {
            appendError(errors, valid, "radius must be finite and non-negative");
        }
        if (!node.direction.isFinite()) {
            appendError(errors, valid, "direction must be finite");
        }
    }
    if (deadSeen != deadInternodes_) {
        appendError(errors, valid, "dead internode counter out of sync");
    }

    // Every non-root must appear exactly once in its parent's child list, and
    // sibling lists must be index-ordered.
    std::vector<std::uint32_t> childListAppearances(count, 0);
    for (Index i = 0; i < count; ++i) {
        Index previous = kInvalidIndex;
        for (Index child = internodes_[i].firstChild; child != kInvalidIndex;
             child = internodes_[child].nextSibling) {
            if (child >= count) {
                appendError(errors, valid, "child link out of range");
                break;
            }
            if (internodes_[child].parent != i) {
                appendError(errors, valid, "child list entry does not point back to parent");
            }
            if (previous != kInvalidIndex && child <= previous) {
                appendError(errors, valid, "sibling list must be index-ordered");
                break;
            }
            ++childListAppearances[child];
            previous = child;
        }
    }
    for (Index i = 0; i < count; ++i) {
        const bool isRoot = internodes_[i].parent == kInvalidIndex;
        const std::uint32_t expected = isRoot ? 0u : 1u;
        if (childListAppearances[i] != expected) {
            appendError(errors, valid, "internode must appear exactly once in its parent's child list");
            break;
        }
    }

    for (const Bud& bud : buds_) {
        if (bud.internode >= count) {
            appendError(errors, valid, "bud references out-of-range internode");
            continue;
        }
        if (bud.state != BudState::Dead && !internodes_[bud.internode].alive) {
            appendError(errors, valid, "live bud attached to dead internode");
        }
        if (!bud.direction.isFinite()) {
            appendError(errors, valid, "bud direction must be finite");
        }
    }

    return valid;
}

} // namespace flora

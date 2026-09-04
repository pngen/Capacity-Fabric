#pragma once

#include <cstdint>
#include <map>
#include <optional>

#include "capacity_fabric/core/identities.hpp"

namespace capacity_fabric {

// Freshness state of evidence used to derive a capacity conclusion. UNKNOWN must
// never be silently treated as high confidence, and stale evidence must never
// contribute usable capacity.
enum class Freshness : uint8_t {
    Fresh,                  // valid and current
    Stale,                  // not current but the identity is known
    RevalidationRequired,   // requires fresh re-publication
    InsufficientEvidence,   // not enough evidence to conclude
    Unknown,
};

inline const char* to_string(Freshness f) noexcept {
    switch (f) {
        case Freshness::Fresh:               return "FRESH";
        case Freshness::Stale:               return "STALE";
        case Freshness::RevalidationRequired:return "REVALIDATION_REQUIRED";
        case Freshness::InsufficientEvidence:return "INSUFFICIENT_EVIDENCE";
        case Freshness::Unknown:             return "UNKNOWN";
    }
    return "UNKNOWN";
}

// Tracks one authority domain's current generation per object id. A publication
// is accepted as current only when its generation is strictly newer than the
// recorded one; generation regression is rejected.
template <class IdT, class GenT>
class GenerationTracker {
public:
    // Attempts to advance the recorded generation for id. Returns true if it
    // replaced the current one. Rejects generation regression.
    bool advance(IdT id, GenT next) {
        const GenT cur = current(id);
        if (next.valid() && cur.valid() && next.value() < cur.value()) {
            return false;
        }
        if (next.valid() && !next.valid()) return false;
        gen_[id] = next;
        return true;
    }

    [[nodiscard]] GenT current(IdT id) const {
        auto it = gen_.find(id);
        return it == gen_.end() ? GenT() : it->second;
    }

    [[nodiscard]] bool isStale(IdT id, GenT observed) const {
        const GenT cur = current(id);
        if (!cur.valid()) return false;
        return observed.value() < cur.value();
    }

private:
    std::map<IdT, GenT> gen_;
};

} // namespace capacity_fabric

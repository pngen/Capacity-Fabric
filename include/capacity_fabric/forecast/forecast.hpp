#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "capacity_fabric/core/provenance.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric {

// Query modes. A query must never present an optimistic scenario as guaranteed
// capacity; each mode carries distinct release-time and confidence semantics.
enum class QueryMode : uint8_t {
    Guaranteed,      // committed-only; releases credited only at their latest bound
    Conservative,    // releases at latest, confidence conservative
    Expected,        // releases at expected time
    Optimistic,      // releases at earliest bound
    WhatIf,          // scenario-driven overlay
    Synthetic,       // synthetic evidence
};

inline const char* to_string(QueryMode m) noexcept {
    switch (m) {
        case QueryMode::Guaranteed:   return "GUARANTEED";
        case QueryMode::Conservative: return "CONSERVATIVE";
        case QueryMode::Expected:     return "EXPECTED";
        case QueryMode::Optimistic:   return "OPTIMISTIC";
        case QueryMode::WhatIf:       return "WHAT_IF";
        case QueryMode::Synthetic:    return "SYNTHETIC";
    }
    return "UNKNOWN";
}

// Which release-boundary a mode uses when constructing a timeline.
[[nodiscard]] inline TimePoint releaseTime(const ReleaseEvent& e, QueryMode mode) noexcept {
    switch (mode) {
        case QueryMode::Optimistic: return e.earliest;
        case QueryMode::Guaranteed:
        case QueryMode::Conservative: return e.latest;
        default: return e.time;  // expected
    }
}

// Confidence degrades monotonically with forecast horizon under an explicit,
// inspectable policy. No claimed statistical precision is invented.
[[nodiscard]] inline double degradeConfidence(Confidence base, TimePoint horizon, TimePoint issueTime) noexcept {
    const double h = static_cast<double>(std::max<TimePoint>(0, horizon - issueTime));
    // Linear degradation to a floor of 0.05 over the span of 30 days in nanos.
    const double days = h / static_cast<double>(30 * kNanosPerSecond);
    double degraded = base.value() - 0.10 * days;
    if (degraded < 0.05) degraded = 0.05;
    if (degraded > 1.0) degraded = 1.0;
    return degraded;
}

// Builds a timeline for the requested query mode given base capacity, reservations
// and release events. Deterministic for identical inputs and mode.
[[nodiscard]] inline Timeline buildTimeline(const Capacity& base,
                                            const std::vector<Reservation>& reservations,
                                            const std::vector<ReleaseEvent>& releases,
                                            QueryMode mode) {
    Timeline tl(base);
    for (const auto& r : reservations) {
        if (r.strength == ReservationStrength::Hard || mode != QueryMode::Guaranteed) {
            if (!r.hold.empty()) tl.addReservation(r);
        }
    }
    for (const auto& e : releases) {
        ReleaseEvent ev = e;
        ev.time = releaseTime(e, mode);
        tl.addRelease(ev);
    }
    tl.freeze();
    return tl;
}

} // namespace capacity_fabric

#pragma once

#include <cstdint>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/units.hpp"

namespace capacity_fabric {

// Explicit multi-concept headroom. Headroom is never collapsed to a single
// percentage; a caller can ask how much remains after reservations, after a
// failure, or how much is required to safely run this workload.
struct Headroom {
    Capacity raw;             // nominal free capacity
    Capacity used;            // currently consumed by active work
    Capacity committed;       // hard reserved/committed
    Capacity safetyMargin;    // policy-defined margin (conservative guard)
    Capacity failureMargin;   // headroom retained for a single-device failure
    Capacity maintenanceMargin; // headroom retained for maintenance
    Capacity conservative;    // raw - used - committed - margins
    Capacity expected;        // expected view (same as conservative here)
    Capacity workloadSpecific; // headroom that still supports the demand shape
    bool capacityConstrained = false;  // conservative <= 0 on some dimension
};

// Computes a conservative headroom: raw - used - committed - safetyMargin, clamped
// at zero per dimension. The capacityConstrained flag is set when any dimension
// reaches zero under the safety margin.
[[nodiscard]] inline Headroom computeHeadroom(const Capacity& raw, const Capacity& used,
                                              const Capacity& committed,
                                              const Capacity& safetyMargin) {
    Headroom h;
    h.raw = raw;
    h.used = used;
    h.committed = committed;
    h.safetyMargin = safetyMargin;

    Capacity afterUsed = raw;
    afterUsed.subtractClamp(used);
    Capacity afterCommitted = afterUsed;
    afterCommitted.subtractClamp(committed);
    Capacity conservative = afterCommitted;
    conservative.subtractClamp(safetyMargin);

    h.conservative = conservative;
    h.expected = conservative;  // deterministic model, no statistical spread here

    // Failure margin: retain one device's worth. Approximated as the minimum
    // non-zero per-dimension amount under a uniform failure assumption.
    Capacity failure = safetyMargin;  // reuse safety margin as an approximation
    h.failureMargin = safetyMargin;
    h.maintenanceMargin = safetyMargin;

    h.capacityConstrained = false;
    for (const auto& [d, v] : conservative.entries()) {
        if (v <= 0.0) h.capacityConstrained = true;
    }
    return h;
}

// Reduces a base headroom by a demand's requirements, producing the headroom that
// still supports the demand shape.
[[nodiscard]] inline Headroom headroomForDemand(const Headroom& base, const Capacity& demandReq) {
    Headroom h = base;
    Capacity ws = base.conservative;
    ws.subtractClamp(demandReq);
    h.workloadSpecific = ws;
    return h;
}

} // namespace capacity_fabric

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/forecast/forecast.hpp"
#include "capacity_fabric/model/freshness.hpp"

namespace capacity_fabric {

// Deterministic feasibility outcome. UNKNOWN must never be reported as FIT, and
// REVALIDATION_REQUIRED / INSUFFICIENT_EVIDENCE are distinct from any FIT/NO_FIT.
enum class Outcome : uint8_t {
    FitNow,
    FitFuture,
    FitWithReducedShape,
    FitWithLowerHeadroom,
    NoFitCapacity,
    NoFitFragmentation,
    NoFitTopology,
    NoFitCapability,
    NoFitMemory,
    NoFitBandwidth,
    NoFitReservation,
    NoFitDuration,
    NoFitSloHeadroom,
    RevalidationRequired,
    InsufficientEvidence,
    Unknown,
};

inline const char* to_string(Outcome o) noexcept {
    switch (o) {
        case Outcome::FitNow:                return "FIT_NOW";
        case Outcome::FitFuture:             return "FIT_FUTURE";
        case Outcome::FitWithReducedShape:   return "FIT_WITH_REDUCED_SHAPE";
        case Outcome::FitWithLowerHeadroom:  return "FIT_WITH_LOWER_HEADROOM";
        case Outcome::NoFitCapacity:         return "NO_FIT_CAPACITY";
        case Outcome::NoFitFragmentation:    return "NO_FIT_FRAGMENTATION";
        case Outcome::NoFitTopology:         return "NO_FIT_TOPOLOGY";
        case Outcome::NoFitCapability:       return "NO_FIT_CAPABILITY";
        case Outcome::NoFitMemory:           return "NO_FIT_MEMORY";
        case Outcome::NoFitBandwidth:        return "NO_FIT_BANDWIDTH";
        case Outcome::NoFitReservation:      return "NO_FIT_RESERVATION";
        case Outcome::NoFitDuration:         return "NO_FIT_DURATION";
        case Outcome::NoFitSloHeadroom:      return "NO_FIT_SLO_HEADROOM";
        case Outcome::RevalidationRequired:  return "REVALIDATION_REQUIRED";
        case Outcome::InsufficientEvidence:  return "INSUFFICIENT_EVIDENCE";
        case Outcome::Unknown:               return "UNKNOWN";
    }
    return "UNKNOWN";
}

// The kind of bottleneck, driving whether the problem is physical capacity,
// fragmentation, topology, capability, policy, or uncertainty.
enum class BottleneckKind : uint8_t {
    Capacity,
    Fragmentation,
    Topology,
    Capability,
    MemoryContiguity,
    Bandwidth,
    Reservation,
    SloHeadroom,
    Duration,
    Policy,
    Uncertainty,
};

inline const char* to_string(BottleneckKind k) noexcept {
    switch (k) {
        case BottleneckKind::Capacity:        return "CAPACITY";
        case BottleneckKind::Fragmentation:   return "FRAGMENTATION";
        case BottleneckKind::Topology:        return "TOPOLOGY";
        case BottleneckKind::Capability:      return "CAPABILITY";
        case BottleneckKind::MemoryContiguity:return "MEMORY_CONTIGUITY";
        case BottleneckKind::Bandwidth:       return "BANDWIDTH";
        case BottleneckKind::Reservation:     return "RESERVATION";
        case BottleneckKind::SloHeadroom:     return "SLO_HEADROOM";
        case BottleneckKind::Duration:        return "DURATION";
        case BottleneckKind::Policy:          return "POLICY";
        case BottleneckKind::Uncertainty:     return "UNCERTAINTY";
    }
    return "UNKNOWN";
}

// A typed, deterministic bottleneck reason plus a human-readable explanation.
// Deterministic explanations are the primary API; opaque heuristic strings are
// never the primary representation.
struct Bottleneck {
    BottleneckKind kind = BottleneckKind::Capacity;
    Dimension dimension = Dimension::AcceleratorCount;
    std::string message;      // deterministic human-readable explanation
    Capacity nominal;
    Capacity usable;
    Capacity committed;
    Capacity stranded;
    // Which reservation creates the blocking interval (invalid if none).
    ReservationId reservationId;
    Interval blockingInterval;
    bool hasReservation = false;
    // Confidence supporting the answer. UNKNOWN confidence is never treated as high.
    Confidence confidence = Confidence(0.0);
    Freshness freshness = Freshness::Fresh;
};

// The result of a feasibility query.
struct FeasibilityResult {
    Outcome outcome = Outcome::Unknown;
    Bottleneck bottleneck;                 // primary reason (if any)
    std::vector<Bottleneck> reasons;       // ordered reasons
    TimePoint fitTime = 0;                 // when it fits (FIT_FUTURE)
    QueryMode mode = QueryMode::Guaranteed;
    Confidence confidence = Confidence(0.0);
    Freshness freshness = Freshness::Fresh;
    bool reducedShape = false;
};

} // namespace capacity_fabric

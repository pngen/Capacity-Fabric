#pragma once

#include <cstdint>

namespace capacity_fabric {

// Provenance of a capacity fact. Categories must never be blurred: a measured
// current free value is not an estimated future value, a reservation is not a
// forecast, and a synthetic topology is not physical evidence.
enum class Provenance : uint8_t {
    Measured,       // physically observed
    Reported,       // reported by an external authority
    Derived,        // derived from other model facts
    Estimated,      // an estimate under an explicit model
    Forecast,       // a forward projection (never a promise)
    Reconstructed,  // recovered from durable state
    Synthetic,      // generated for a reference/synthetic scenario
    Unknown,        // not known; must remain unknown, never become capacity
};

inline const char* to_string(Provenance p) noexcept {
    switch (p) {
        case Provenance::Measured:      return "MEASURED";
        case Provenance::Reported:      return "REPORTED";
        case Provenance::Derived:       return "DERIVED";
        case Provenance::Estimated:     return "ESTIMATED";
        case Provenance::Forecast:      return "FORECAST";
        case Provenance::Reconstructed: return "RECONSTRUCTED";
        case Provenance::Synthetic:     return "SYNTHETIC";
        case Provenance::Unknown:       return "UNKNOWN";
    }
    return "UNKNOWN";
}

// Evidence kind used for labelling real-vs-derived-vs-unsupported evidence,
// especially in the CUDA proof and synthetic fleet scenarios.
enum class EvidenceKind : uint8_t {
    Real,           // physically observed on real hardware
    Derived,        // derived from real evidence
    Estimated,      // estimated
    Forecast,       // projected
    Synthetic,      // generated
    Unknown,        // unknowable
    Unsupported,    // capability absent/unsupported on this platform
};

inline const char* to_string(EvidenceKind k) noexcept {
    switch (k) {
        case EvidenceKind::Real:        return "REAL";
        case EvidenceKind::Derived:     return "DERIVED";
        case EvidenceKind::Estimated:   return "ESTIMATED";
        case EvidenceKind::Forecast:    return "FORECAST";
        case EvidenceKind::Synthetic:   return "SYNTHETIC";
        case EvidenceKind::Unknown:     return "UNKNOWN";
        case EvidenceKind::Unsupported: return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

} // namespace capacity_fabric

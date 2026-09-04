#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/units.hpp"

namespace capacity_fabric {

// Accelerator class / architecture capability identifiers (opaque, reported by
// the device capability evidence). Two devices are "compatible" if their class
// sits in the intersection of the demand's allowed classes.
struct AcceleratorClass {
    std::string name;
    std::string architecture;   // e.g. "sm_120", "sm_90"
    std::string capability;     // e.g. "fp8", "tensor_core"

    bool operator==(const AcceleratorClass& o) const {
        return name == o.name && architecture == o.architecture && capability == o.capability;
    }
    bool operator<(const AcceleratorClass& o) const {
        if (name != o.name) return name < o.name;
        if (architecture != o.architecture) return architecture < o.architecture;
        return capability < o.capability;
    }
};

// A workload-demand profile describes the shape Capacity Fabric should test
// against available/projected capacity. It is NOT a reservation.
struct WorkloadDemand {
    WorkloadDemandId id;
    WorkloadDemandGeneration generation;

    // Scalar per-dimension requirements.
    Capacity requirements;

    // Contiguous allocation requirement (e.g. a single contiguous VRAM block).
    bool requiresContiguousVram = false;
    ByteCount minContiguousVram = ByteCount::zero();

    // Requirement: this many devices must all be part of one topology-local group.
    DeviceCount minAccelerators = DeviceCount(1);
    std::optional<DeviceCount> topologyGroupSize;  // if set, require a group of this many

    // Allowed accelerator classes; empty means any observed class is acceptable.
    std::vector<AcceleratorClass> allowedClasses;

    // Required capability/architecture substring; empty means no constraint.
    std::string requiredCapability;

    // Colocation: devices that must share a node/domain with this workload.
    std::set<ResourceId> colocation;
    // Anti-affinity: resources that must NOT host this workload.
    std::set<ResourceId> antiAffinity;

    // Residency requirements.
    bool requiresModelResidency = false;
    bool requiresAdapterResidency = false;

    // SLO headroom requirement, supplied externally by SLO Fabric (0 = none).
    CapacityFraction sloHeadroomRequirement = CapacityFraction(0.0);

    // Maximum acceptable fragmentation ratio in [0,1]; if set and the observed
    // fragmentation exceeds it, the fit is rejected.
    std::optional<double> maxAcceptableFragmentation;

    // Placement flexibility (number of alternative candidate groups tolerated).
    DeviceCount placementFlexibility = DeviceCount(1);

    // Expected duration of the workload (for continuous-fit windows).
    std::optional<DurationNs> duration;

    // Elastic shape: optional min/target/max shrink/expand per dimension.
    // If target is not satisfiable but min is, the fit may be accepted as
    // FIT_WITH_REDUCED_SHAPE.
    std::optional<Capacity> minShape;
    std::optional<Capacity> maxShape;

    [[nodiscard]] bool valid() const {
        if (!id.valid() || !generation.valid()) return false;
        if (minAccelerators.value() == 0) return false;
        if (requiresContiguousVram && minContiguousVram == ByteCount::zero()) return false;
        if (maxAcceptableFragmentation.has_value()) {
            const double f = *maxAcceptableFragmentation;
            if (!std::isfinite(f) || f < 0.0 || f > 1.0) return false;
        }
        return true;
    }
};

} // namespace capacity_fabric

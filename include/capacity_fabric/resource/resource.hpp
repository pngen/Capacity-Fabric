#pragma once

#include <map>
#include <string>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/provenance.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/demand/demand.hpp"

namespace capacity_fabric {

// Health state is reported by Accelerator Health; Capacity Fabric consumes it
// but does not assess health itself.
enum class HealthState : uint8_t {
    Healthy,
    Degraded,
    Draining,
    Maintenance,
    Failed,
    Unknown,
};

inline const char* to_string(HealthState h) noexcept {
    switch (h) {
        case HealthState::Healthy:     return "HEALTHY";
        case HealthState::Degraded:    return "DEGRADED";
        case HealthState::Draining:    return "DRAINING";
        case HealthState::Maintenance: return "MAINTENANCE";
        case HealthState::Failed:      return "FAILED";
        case HealthState::Unknown:     return "UNKNOWN";
    }
    return "UNKNOWN";
}

// The kind of resource record.
enum class ResourceKind : uint8_t {
    Physical,     // a real physical resource
    Pool,         // a governed resource pool / abstraction
    Virtual,      // explicitly allowed virtual/overcommit
};

// Device capability evidence (reported by GPU Fleet Agent / Runtime Registry).
struct DeviceCapability {
    std::string architecture;
    std::string className;
    std::string capability;
    // Set of capabilities this device supports.
    std::vector<std::string> markers;
};

// A memory domain (e.g. a NUMA node or device-local memory).
struct MemoryDomainInfo {
    MemoryDomainId id;
    MemoryDomainGeneration generation;
    ByteCount total = ByteCount::zero();
    ByteCount free = ByteCount::zero();
    std::vector<DeviceId> members;
};

// A single accelerator/device.
struct DeviceInfo {
    DeviceId id;
    DeviceGeneration generation;
    NodeId nodeId;
    DeviceCapability capability;
    ByteCount vramTotal = ByteCount::zero();
    ByteCount vramFree = ByteCount::zero();
    std::vector<ByteCount> freeBlocks;  // free contiguous blocks (for fragmentation)
    HealthState health = HealthState::Unknown;
    MemoryDomainId memoryDomain;  // may be invalid if not bound
    Provenance provenance = Provenance::Unknown;
    bool poolGoverned = true;      // part of a governed test/operational pool
    bool drained = false;
    bool maintenance = false;
};

// A compute node.
struct NodeInfo {
    NodeId id;
    NodeGeneration generation;
    std::vector<DeviceId> devices;
    std::vector<MemoryDomainId> memoryDomains;
    ByteCount pinnedHostFree = ByteCount::zero();
    ByteCount pageableHostFree = ByteCount::zero();
    HealthState health = HealthState::Unknown;
    std::string rack;   // failure domain
    std::string topologyGroup;  // topology-local group label
    Provenance provenance = Provenance::Unknown;
};

// A resource identity that Capacity Fabric tracks. Each resource carries its
// own generation, the evidence provenance, and the source incarnation (worker
// boot identity) that produced the current evidence.
struct ResourceInfo {
    ResourceId id;
    ResourceGeneration generation;
    FleetId fleetId;
    NodeId nodeId;
    DeviceId deviceId;
    ResourceKind kind = ResourceKind::Physical;
    DeviceCapability capability;

    // Nominal (installed) capacity.
    Capacity nominal;
    // Currently free capacity (dynamic evidence, must be revalidated).
    Capacity free;
    // Free contiguous memory blocks (for fragmentation analysis).
    std::vector<ByteCount> freeBlocks;
    // Committed / already-allocated capacity (consumed).
    Capacity committed;
    // Unavailable capacity (health, maintenance, drain, policy).
    Capacity unavailable;

    HealthState health = HealthState::Unknown;
    bool drained = false;
    bool maintenance = false;

    // Source incarnation for freshness.
    WorkerId worker;
    WorkerBootId bootId;
    Provenance provenance = Provenance::Unknown;

    // Wall-clock observation (authority timeline).
    TimePoint observedAt = 0;

    [[nodiscard]] bool isCurrentDynamic() const { return bootId.valid(); }
};

// A topology-locality linkage between devices (e.g. NVLink pair).
struct LinkInfo {
    LinkId id;
    LinkGeneration generation;
    DeviceId a;
    DeviceId b;
    ByteRate bandwidth = ByteRate(0.0);
    bool shared = false;   // part of a shared group
};

} // namespace capacity_fabric

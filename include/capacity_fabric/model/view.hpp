#pragma once

#include <cstdint>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/provenance.hpp"
#include "capacity_fabric/model/freshness.hpp"
#include "capacity_fabric/resource/resource.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric {

// An aggregated, authoritative view of the fleet at a point in time, produced by
// the CapacityModel from current evidence. Views are immutable once built, so a
// query never sees a half-updated model.
struct FleetView {
    std::vector<DeviceInfo> devices;
    std::vector<ResourceInfo> resources;
    Capacity available;      // aggregate usable free (healthy, uncommitted)
    Capacity nominal;
    Capacity committed;
    Capacity unavailable;
    Capacity currentUse;      // nominal - free - committed - unavailable, clamped

    std::vector<Reservation> reservations;   // active reservations (from Reservation Fabric)
    std::vector<ReleaseEvent> releases;      // expected capacity-return events
    std::vector<MaintenanceWindow> maintenance;

    TimePoint now = 0;
    std::size_t maxTopologyGroupSize = 0;    // largest topology-local group observed
    Freshness freshness = Freshness::Fresh;
    bool staleDynamic = false;               // some dynamic evidence is stale
    Provenance provenance = Provenance::Unknown;
};

} // namespace capacity_fabric

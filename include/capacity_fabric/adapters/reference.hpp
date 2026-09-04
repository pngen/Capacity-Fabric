#pragma once

#include <cstdint>
#include <string>

#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/resource/resource.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric {
class CapacityModel;
}

namespace capacity_fabric::reference {

// Reference adapter builders produce deterministic synthetic evidence for the
// standalone tests, examples, and the reference distributed deployment. All such
// evidence is labelled SYNTHETIC unless a real source is substituted.

inline ResourceInfo makeResource(ResourceId id, ResourceGeneration gen, FleetId fleet, NodeId node,
                                 DeviceId device, WorkerId worker, WorkerBootId boot,
                                 const DeviceCapability& cap, ByteCount vramNominal,
                                 ByteCount vramFree, const std::vector<ByteCount>& blocks,
                                 HealthState health = HealthState::Healthy) {
    ResourceInfo r;
    r.id = id;
    r.generation = gen;
    r.fleetId = fleet;
    r.nodeId = node;
    r.deviceId = device;
    r.kind = ResourceKind::Physical;
    r.capability = cap;
    r.nominal.setVram(vramNominal);
    r.nominal.setAccelerators(DeviceCount(1));
    r.free.setVram(vramFree);
    r.free.setAccelerators(DeviceCount(1));
    r.freeBlocks = blocks;
    r.health = health;
    r.worker = worker;
    r.bootId = boot;
    r.provenance = Provenance::Synthetic;
    r.drained = (health == HealthState::Draining);
    r.maintenance = (health == HealthState::Maintenance);
    return r;
}

inline Reservation makeReservation(ReservationId id, ReservationGeneration gen, Interval iv,
                                   double holdUnits, ReservationStrength strength = ReservationStrength::Hard) {
    Reservation r;
    r.id = id;
    r.generation = gen;
    r.interval = iv;
    r.hold.change(Dimension::AcceleratorCount, holdUnits);
    r.strength = strength;
    r.provenance = Provenance::Reported;
    return r;
}

inline ReleaseEvent makeRelease(ForecastSourceId src, TimePoint expected, TimePoint earliest,
                               TimePoint latest, double units, Confidence conf = Confidence(0.7)) {
    ReleaseEvent e;
    e.sourceId = src;
    e.time = expected;
    e.earliest = earliest;
    e.latest = latest;
    e.release.change(Dimension::AcceleratorCount, units);
    e.confidence = conf;
    e.provenance = Provenance::Forecast;
    return e;
}

inline WorkloadDemand makeDemand(WorkloadDemandId id, WorkloadDemandGeneration gen, double units,
                                 DeviceCount minAccels, bool requireContiguous = false,
                                 ByteCount minContig = ByteCount::zero()) {
    WorkloadDemand d;
    d.id = id;
    d.generation = gen;
    d.requirements.setAccelerators(DeviceCount(static_cast<uint64_t>(units)));
    d.minAccelerators = minAccels;
    d.requiresContiguousVram = requireContiguous;
    d.minContiguousVram = minContig;
    return d;
}

// Seeds a CapacityModel with a deterministic reference calendar of workers,
// resources, reservations, and release forecasts (used by the CLI and tests).
// The model must be empty on entry.
void seedReferenceModel(capacity_fabric::CapacityModel& model);

const char* description();

} // namespace capacity_fabric::reference

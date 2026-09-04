#pragma once

#include <vector>

#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/provenance.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/model/view.hpp"
#include "capacity_fabric/resource/resource.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric {

struct ScenarioAction {
    enum class Kind : uint8_t {
        RemoveResource,   // drop a resource/device
        AddResource,      // add a resource/device
        CancelReservation,
        AddReservation,
        DelayRelease,
        DegradeDevice,    // mark a device Failed
        SetHeadroom,      // raise/lower an SLO headroom obligation
        MoveMaintenance,
    };
    Kind kind;
    ResourceId resource;         // RemoveResource / DegradeDevice
    ResourceInfo added;          // AddResource
    ReservationId reservation;   // CancelReservation
    Reservation addReservation;  // AddReservation
    ForecastSourceId source;     // DelayRelease
    DurationNs delay = 0;        // DelayRelease
    CapacityFraction sloHeadroom = CapacityFraction(0.0);  // SetHeadroom
    Interval maintenance;        // MoveMaintenance
};

// An immutable what-if overlay. Scenario state is always marked WHAT_IF/SYNTHETIC
// and can never become authoritative unless explicitly promoted.
struct Scenario {
    ScenarioId id;
    ScenarioGeneration generation;
    std::vector<ScenarioAction> actions;
    Provenance provenance = Provenance::Synthetic;
    bool authoritative = false;   // always false unless explicitly promoted

    [[nodiscard]] bool valid() const {
        return id.valid() && generation.valid() && !authoritative;
    }
};

// Applies a scenario to an immutable view, producing a new view (the input is
// never mutated). The result is labelled SYNTHETIC/WHAT_IF.
[[nodiscard]] inline FleetView applyScenario(const FleetView& base, const Scenario& s) {
    FleetView v = base;   // copy — the authoritative view is untouched
    for (const auto& a : s.actions) {
        switch (a.kind) {
            case ScenarioAction::Kind::RemoveResource: {
                // Collect the device ids owned by the removed resource.
                std::vector<DeviceId> owned;
                for (const auto& r : v.resources) {
                    if (r.id == a.resource) owned.push_back(r.deviceId);
                }
                auto& vec = v.resources;
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                          [&](const ResourceInfo& r) { return r.id == a.resource; }), vec.end());
                auto& dev = v.devices;
                dev.erase(std::remove_if(dev.begin(), dev.end(),
                          [&](const DeviceInfo& d) {
                              return std::find(owned.begin(), owned.end(), d.id) != owned.end();
                          }), dev.end());
                break;
            }
            case ScenarioAction::Kind::AddResource:
                v.resources.push_back(a.added);
                break;
            case ScenarioAction::Kind::CancelReservation:
                v.reservations.erase(std::remove_if(v.reservations.begin(), v.reservations.end(),
                                    [&](const Reservation& r) { return r.id == a.reservation; }),
                                    v.reservations.end());
                break;
            case ScenarioAction::Kind::AddReservation:
                v.reservations.push_back(a.addReservation);
                break;
            case ScenarioAction::Kind::DelayRelease:
                for (auto& e : v.releases) {
                    if (e.sourceId == a.source) {
                        e.time += a.delay;
                        e.earliest += a.delay;
                        e.latest += a.delay;
                    }
                }
                break;
            case ScenarioAction::Kind::DegradeDevice:
                for (auto& d : v.devices) {
                    if (d.id.valid()) {
                        for (auto& r : v.resources) {
                            if (r.deviceId == d.id) {
                                d.health = HealthState::Failed;
                                r.health = HealthState::Failed;
                            }
                        }
                    }
                }
                break;
            case ScenarioAction::Kind::SetHeadroom:
                break;   // handled at query level via demand overlay
            case ScenarioAction::Kind::MoveMaintenance:
                v.maintenance.push_back(MaintenanceWindow{a.maintenance, CapacityDelta{}, true});
                break;
        }
    }
    v.provenance = Provenance::Synthetic;
    // Re-aggregate the modified view so added/removed resources take effect.
    v.devices.clear();
    v.available = Capacity{};
    v.nominal = Capacity{};
    v.committed = Capacity{};
    v.unavailable = Capacity{};
    for (const auto& r : v.resources) {
        v.nominal.add(r.nominal);
        v.committed.add(r.committed);
        v.unavailable.add(r.unavailable);
        DeviceInfo d;
        d.id = r.deviceId;
        d.nodeId = r.nodeId;
        d.capability = r.capability;
        d.vramTotal = r.nominal.vram();
        d.vramFree = r.free.vram();
        d.health = r.health;
        d.provenance = r.provenance;
        d.drained = r.drained;
        d.maintenance = r.maintenance;
        d.freeBlocks = r.freeBlocks;
        v.devices.push_back(d);
        const bool usable = r.health != HealthState::Failed && !r.drained &&
                            !r.maintenance && r.health != HealthState::Unknown;
        if (usable) v.available.add(r.free);
    }
    Capacity curUse = v.nominal;
    curUse.subtractClamp(v.available);
    curUse.subtractClamp(v.committed);
    curUse.subtractClamp(v.unavailable);
    v.currentUse = curUse;
    return v;
}

} // namespace capacity_fabric

#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/provenance.hpp"
#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/model/freshness.hpp"
#include "capacity_fabric/model/view.hpp"
#include "capacity_fabric/query/earliest_fit.hpp"
#include "capacity_fabric/query/feasibility.hpp"
#include "capacity_fabric/query/outcome.hpp"
#include "capacity_fabric/resource/resource.hpp"
#include "capacity_fabric/scenario/scenario.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric {

// Tracks the live incarnation of a source (worker) and whether it is currently
// connected/authoritative.
struct SourceState {
    WorkerBootId bootId;
    bool alive = false;
};

// The CapacityModel is the authoritative controller for Capacity Fabric. It owns
// normalized resource evidence, generations, reservations, release forecasts, and
// demand profiles, and it computes usable capacity, headroom, feasibility, and
// forecasts. It never commits/cancels reservations and never owns live work.
class CapacityModel {
public:
    CapacityModel();

    // ---- Authority / incarnation ---------------------------------------------
    void setEpoch(CoordinatorEpoch e) { epoch_ = e; }
    [[nodiscard]] CoordinatorEpoch epoch() const { return epoch_; }

    bool registerWorker(WorkerId w, WorkerBootId b);
    bool unregisterWorker(WorkerId w);
    void markWorkerDead(WorkerId w);
    [[nodiscard]] bool workerAlive(WorkerId w) const;
    [[nodiscard]] bool isFreshBoot(WorkerId w, WorkerBootId b) const;

    // ---- Evidence publication --------------------------------------------------
    // Returns false (effect) and leaves state unchanged if the publication is
    // stale (boot mismatch, dead source, generation regression, bad epoch).
    bool publishResource(const ResourceInfo& r);
    bool advanceResourceGeneration(ResourceId id);
    bool publishReservation(const Reservation& res);
    bool publishRelease(const ReleaseEvent& rel);
    bool registerDemand(const WorkloadDemand& d);
    bool registerTopology(const std::vector<LinkInfo>& links, TopologyGeneration gen);
    bool setHealth(DeviceId id, HealthState h, HealthGeneration gen);

    // ---- Queries ---------------------------------------------------------------
    [[nodiscard]] FleetView buildView(TimePoint now) const;
    [[nodiscard]] FeasibilityResult queryFeasibility(const WorkloadDemand& d, QueryMode mode,
                                                     TimePoint time) const;
    [[nodiscard]] FeasibilityResult queryFeasibilityNow(const WorkloadDemand& d) const;
    [[nodiscard]] EarliestFitResult queryEarliestFit(const WorkloadDemand& d, QueryMode mode,
                                                     TimePoint earliestStart, DurationNs duration,
                                                     TimePoint horizon) const;
    [[nodiscard]] Capacity queryCapacity(QueryMode mode, TimePoint time) const;
    [[nodiscard]] Headroom queryHeadroom(const WorkloadDemand& d, QueryMode mode) const;
    [[nodiscard]] Capacity queryProjectedCapacity(QueryMode mode, TimePoint time) const;
    [[nodiscard]] std::vector<Bottleneck> explain(const WorkloadDemand& d, QueryMode mode,
                                                  TimePoint time) const;

    [[nodiscard]] std::vector<Reservation> currentReservations() const;
    [[nodiscard]] std::vector<ReleaseEvent> currentReleases() const;
    [[nodiscard]] std::vector<ResourceInfo> currentResources() const;
    [[nodiscard]] std::map<ResourceId, ResourceGeneration> resourceGenerations() const;

    // ---- Scenario ---------------------------------------------------------------
    [[nodiscard]] Scenario createScenario(std::vector<ScenarioAction> actions) const;
    [[nodiscard]] FeasibilityResult evaluateScenario(const Scenario& s, const WorkloadDemand& d,
                                                     QueryMode mode, TimePoint time) const;

    // ---- Persistence -------------------------------------------------------------
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    [[nodiscard]] bool recovered() const { return recovered_; }
    [[nodiscard]] std::string digest() const;   // stable integrity digest of durable state

    // ---- Forecast history / evaluation ------------------------------------------
    void recordForecast(const CapacityForecastId& id, TimePoint at, Capacity expected);
    void recordObserved(TimePoint at, Capacity observed);
    [[nodiscard]] std::vector<std::tuple<TimePoint, Capacity, Capacity>> forecastHistory() const;

    // ---- Deterministic current state for inspection ------------------------------
    [[nodiscard]] std::string describe() const;

private:
    [[nodiscard]] const ResourceInfo* resource(ResourceId id) const;
    [[nodiscard]] Capacity aggregateAvailable(const FleetView& v) const;

    mutable std::mutex mutex_;
    CoordinatorEpoch epoch_;
    bool recovered_ = false;

    // Identity / generations.
    std::map<ResourceId, ResourceInfo> resources_;
    std::map<DeviceId, DeviceInfo> devices_;
    std::map<NodeId, NodeInfo> nodes_;
    std::map<LinkId, LinkInfo> links_;
    GenerationTracker<ResourceId, ResourceGeneration> resourceGen_;
    GenerationTracker<ReservationId, ReservationGeneration> reservationGen_;
    GenerationTracker<TopologyId, TopologyGeneration> topologyGen_;
    GenerationTracker<DeviceId, HealthGeneration> healthGen_;

    std::map<WorkerId, SourceState> sources_;
    std::map<WorkloadDemandId, WorkloadDemand> demands_;

    std::vector<Reservation> reservations_;
    std::vector<ReleaseEvent> releases_;
    std::vector<MaintenanceWindow> maintenance_;
    TopologyGeneration topoGen_;

    // Forecast history.
    std::vector<std::tuple<TimePoint, Capacity, Capacity>> forecastHistory_;  // (at, expected, observed)
};

} // namespace capacity_fabric

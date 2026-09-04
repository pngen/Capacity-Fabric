#include "test_util.hpp"

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

using namespace capacity_fabric;

static void testScenarioIsolation() {
    CapacityModel model;
    reference::seedReferenceModel(model);
    const std::size_t before = model.currentResources().size();
    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1),
                                                   4.0, DeviceCount(4));
    CHECK(model.queryFeasibilityNow(d).outcome == Outcome::FitNow);

    // Removing resource 3 leaves only 3 accelerators -> no longer fits.
    ScenarioAction act;
    act.kind = ScenarioAction::Kind::RemoveResource;
    act.resource = ResourceId(3);
    auto scen = model.createScenario({act});
    FeasibilityResult sr = model.evaluateScenario(scen, d, QueryMode::Expected, 0);
    CHECK(sr.outcome == Outcome::NoFitCapacity);
    CHECK(sr.mode == QueryMode::WhatIf);
    // The authoritative model must be unchanged (scenario never mutates state).
    CHECK(model.currentResources().size() == before);
    CHECK(model.queryFeasibilityNow(d).outcome == Outcome::FitNow);
}

static void testScenarioAddResource() {
    CapacityModel model;
    reference::seedReferenceModel(model);
    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1),
                                                   5.0, DeviceCount(5));
    CHECK(model.queryFeasibilityNow(d).outcome == Outcome::NoFitCapacity);
    // Add a 5th device through a scenario -> fits.
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};
    ResourceInfo extra = reference::makeResource(ResourceId(99), ResourceGeneration(1), FleetId(1),
                                                 NodeId(3), DeviceId(99), WorkerId(9), WorkerBootId(99),
                                                 cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                                 {ByteCount(16ULL << 30)});
    ScenarioAction act;
    act.kind = ScenarioAction::Kind::AddResource;
    act.added = extra;
    auto scen = model.createScenario({act});
    FeasibilityResult sr = model.evaluateScenario(scen, d, QueryMode::Expected, 0);
    CHECK(sr.outcome == Outcome::FitNow);
    // Authoritative state unchanged.
    CHECK(model.queryFeasibilityNow(d).outcome == Outcome::NoFitCapacity);
}

static void testScenarioCancelReservation() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    ResourceInfo pool;
    pool.id = ResourceId(1); pool.generation = ResourceGeneration(1);
    pool.fleetId = FleetId(1); pool.nodeId = NodeId(1); pool.deviceId = DeviceId(1);
    pool.nominal.setAccelerators(DeviceCount(100));
    pool.free.setAccelerators(DeviceCount(100));
    pool.health = HealthState::Healthy;
    pool.worker = w; pool.bootId = b; pool.provenance = Provenance::Synthetic;
    [[maybe_unused]] bool okPool = model.publishResource(pool);
    CHECK(model.publishReservation(reference::makeReservation(ReservationId(1), ReservationGeneration(1),
                                    Interval(0, timeFromMillis(8000)), 60.0)));
    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 70.0, DeviceCount(1));
    // During the reservation only 40 units remain.
    CHECK(model.queryFeasibility(d, QueryMode::Guaranteed, timeFromMillis(3000)).outcome == Outcome::NoFitCapacity);
    // Scenario cancels the reservation -> fits during that window.
    ScenarioAction act;
    act.kind = ScenarioAction::Kind::CancelReservation;
    act.reservation = ReservationId(1);
    auto scen = model.createScenario({act});
    FeasibilityResult sr = model.evaluateScenario(scen, d, QueryMode::Guaranteed, timeFromMillis(3000));
    CHECK(sr.outcome == Outcome::FitNow);
    // Authoritative reservation remains.
    CHECK(model.currentReservations().size() == 1);
}

int main() {
    testScenarioIsolation();
    testScenarioAddResource();
    testScenarioCancelReservation();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}

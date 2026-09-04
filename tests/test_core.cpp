#include "test_util.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/capacity_fabric.hpp"

using namespace capacity_fabric;

static void testUnits() {
    // Reject NaN / inf / negative / out-of-range fractions.
    bool threw = false;
    try { CapacityFraction f(1.5); (void)f; } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Confidence c(NAN); (void)c; } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { ByteRate r(-1.0); (void)r; } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // ByteCount overflow rejection.
    ByteCount a(std::numeric_limits<uint64_t>::max());
    ByteCount b(1);
    ByteCount out;
    CHECK(!a.add(b, out));
    CHECK(a.subtract(a, out) && out == ByteCount::zero());

    // Interval validation.
    threw = false;
    try { Interval bad(5, 5); (void)bad; } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    Interval ok(1, 5);
    CHECK(ok.length() == 4);
    CHECK(ok.overlaps(Interval(3, 7)));
    CHECK(!ok.overlaps(Interval(5, 9)));
}

static void testFragmentation() {
    // 4 devices, each 12 GiB free, one contiguous block of 12 GiB each.
    std::vector<DeviceInfo> devices;
    for (int i = 0; i < 4; ++i) {
        DeviceInfo d;
        d.id = DeviceId(i + 1);
        d.health = HealthState::Healthy;
        d.vramFree = ByteCount(12ULL << 30);  // 12 GiB
        d.freeBlocks = {ByteCount(12ULL << 30)};
        devices.push_back(d);
    }
    // Aggregate free memory = 48 GiB, but no single block >= 20 GiB.
    WorkloadDemand demand = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1),
                                                  1.0, DeviceCount(1), true, ByteCount(20ULL << 30));
    FragmentationReport r = analyzeFragmentation(devices, demand, 0);
    // Each device block = 12GiB < 20GiB -> shape not satisfiable.
    CHECK(!r.shapeSatisfiable);
    CHECK(r.totalFreeMemory.value() == (48ULL << 30));
    CHECK(r.stranded.value() == (48ULL << 30));
    // Now a demand requiring 8 GiB per device fits.
    WorkloadDemand demand8 = reference::makeDemand(WorkloadDemandId(2), WorkloadDemandGeneration(1),
                                                   1.0, DeviceCount(1), true, ByteCount(8ULL << 30));
    FragmentationReport r8 = analyzeFragmentation(devices, demand8, 0);
    CHECK(r8.shapeSatisfiable);
    // Four usable devices each contribute a usable 12 GiB block.
    CHECK(r8.usableForShape.value() == (48ULL << 30));
}

static void testReservationInteraction() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(11);
    model.registerWorker(w, b);
    // A governed pool of 100 units.
    ResourceInfo pool;
    pool.id = ResourceId(1);
    pool.generation = ResourceGeneration(1);
    pool.fleetId = FleetId(1);
    pool.nodeId = NodeId(1);
    pool.deviceId = DeviceId(1);
    pool.capability = DeviceCapability{"sm_120", "pool", "units", {"units"}};
    pool.nominal.setAccelerators(DeviceCount(100));
    pool.free.setAccelerators(DeviceCount(100));
    pool.health = HealthState::Healthy;
    pool.worker = w;
    pool.bootId = b;
    pool.provenance = Provenance::Synthetic;
    pool.freeBlocks = {ByteCount(100)};   // arbitrary block marker
    CHECK(model.publishResource(pool));

    // Demand for 70 units fits now (no reservation yet). The pool is a single
    // governed unit pool, so the device-count requirement is one.
    WorkloadDemand d = reference::makeDemand(WorkloadDemandId(10), WorkloadDemandGeneration(1), 70.0, DeviceCount(1));
    CHECK(model.queryFeasibilityNow(d).outcome == Outcome::FitNow);

    // Future hard reservation of 40 units overlapping [t=5s, t=15s).
    Reservation res = reference::makeReservation(ReservationId(7), ReservationGeneration(1),
                                                 Interval(timeFromMillis(5000), timeFromMillis(15000)),
                                                 40.0, ReservationStrength::Hard);
    CHECK(model.publishReservation(res));

    // At t=6s only 60 units remain; 70-unit demand must not fit.
    CHECK(model.queryFeasibility(d, QueryMode::Guaranteed, timeFromMillis(6000)).outcome == Outcome::NoFitCapacity);
    // At t=20s the reservation has ended; the demand fits again.
    CHECK(model.queryFeasibility(d, QueryMode::Guaranteed, timeFromMillis(20000)).outcome == Outcome::FitFuture);
    // Earliest fit at or after t=0 with a 20s window must skip the reservation
    // window, so it begins exactly at the reservation end (t=15s).
    auto ef = model.queryEarliestFit(d, QueryMode::Guaranteed, 0, timeFromMillis(20000), timeFromMillis(60000));
    CHECK(ef.found);
    CHECK(ef.start == timeFromMillis(15000));
}

static void testForecastUncertainty() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    ResourceInfo pool;
    pool.id = ResourceId(1);
    pool.generation = ResourceGeneration(1);
    pool.fleetId = FleetId(1);
    pool.nodeId = NodeId(1);
    pool.deviceId = DeviceId(1);
    pool.nominal.setAccelerators(DeviceCount(100));
    pool.free.setAccelerators(DeviceCount(100));
    pool.health = HealthState::Healthy;
    pool.worker = w;
    pool.bootId = b;
    pool.provenance = Provenance::Synthetic;
    CHECK(model.publishResource(pool));

    // Expected release of 40 units between T+8..T+12.
    ReleaseEvent rel = reference::makeRelease(ForecastSourceId(20), timeFromMillis(10000),
                                              timeFromMillis(8000), timeFromMillis(12000), 40.0,
                                              Confidence(0.7));
    CHECK(model.publishRelease(rel));

    WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 130.0, DeviceCount(1));
    // Conservative (assumes release at latest, T+12): not fit at T+11, fit at T+12.
    CHECK(model.queryFeasibility(d, QueryMode::Conservative, timeFromMillis(11000)).outcome == Outcome::NoFitCapacity);
    CHECK(model.queryFeasibility(d, QueryMode::Conservative, timeFromMillis(12000)).outcome == Outcome::FitFuture);
    // Expected (T+10): fit at T+10.
    CHECK(model.queryFeasibility(d, QueryMode::Expected, timeFromMillis(10000)).outcome == Outcome::FitFuture);
    // Optimistic (T+8): fit at T+8.
    CHECK(model.queryFeasibility(d, QueryMode::Optimistic, timeFromMillis(8000)).outcome == Outcome::FitFuture);
    // GUARANTEED must never claim T+8 as available: at T+8 guaranteed uses latest release, so no fit.
    CHECK(model.queryFeasibility(d, QueryMode::Guaranteed, timeFromMillis(8000)).outcome == Outcome::NoFitCapacity);
}

static void testEarliestFitMatchesReference() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    ResourceInfo pool;
    pool.id = ResourceId(1);
    pool.generation = ResourceGeneration(1);
    pool.fleetId = FleetId(1);
    pool.nodeId = NodeId(1);
    pool.deviceId = DeviceId(1);
    pool.nominal.setAccelerators(DeviceCount(50));
    pool.free.setAccelerators(DeviceCount(50));
    pool.health = HealthState::Healthy;
    pool.worker = w; pool.bootId = b; pool.provenance = Provenance::Synthetic;
    CHECK(model.publishResource(pool));
    // Two reservations create preferred windows.
    CHECK(model.publishReservation(reference::makeReservation(ReservationId(1), ReservationGeneration(1),
                                    Interval(timeFromMillis(2000), timeFromMillis(6000)), 30.0)));
    CHECK(model.publishReservation(reference::makeReservation(ReservationId(2), ReservationGeneration(1),
                                    Interval(timeFromMillis(10000), timeFromMillis(14000)), 30.0)));
    WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 30.0, DeviceCount(30));
    const auto fast = model.queryEarliestFit(d, QueryMode::Guaranteed, 0, timeFromMillis(6000), timeFromMillis(40000));
    // Slow reference over the same model: reconstruct the guaranteed timeline from
    // the model's base capacity and reservations, then run the grid reference.
    const auto res = model.currentReservations();
    Timeline reftl(model.queryCapacity(QueryMode::Guaranteed, 0));
    for (const auto& rr : res) reftl.addReservation(rr);
    reftl.freeze();
    const auto slow = slowReferenceEarliestFit(reftl, d, 0, timeFromMillis(6000), timeFromMillis(40000),
                                               timeFromMillis(1));
    CHECK(fast.found);
    CHECK(slow.found);
    CHECK(fast.start == slow.start);
    // The earliest 6s window that avoids both reservations starts at the second
    // reservation's end (t=14s).
    CHECK(fast.start == timeFromMillis(14000));
}

static void testHeadroom() {
    Capacity base;
    base.setAccelerators(DeviceCount(100));
    Capacity margin;
    margin.setAccelerators(DeviceCount(20));
    Headroom h = computeHeadroom(base, Capacity{}, Capacity{}, margin);
    CHECK(h.conservative.amount(Dimension::AcceleratorCount) == 80.0);
    CHECK(!h.capacityConstrained);
    // A demand of 90 leaves only 10 conservative headroom before margin.
    Capacity demand;
    demand.setAccelerators(DeviceCount(90));
    Headroom h2 = headroomForDemand(h, demand);
    CHECK(h2.workloadSpecific.amount(Dimension::AcceleratorCount) == 0.0);
}

static void testPersistenceRoundTrip() {
    CapacityModel model;
    reference::seedReferenceModel(model);
    const std::string path = "test_roundtrip.cf";
    CHECK(model.save(path));
    CapacityModel m2;
    CHECK(m2.load(path));
    CHECK(m2.recovered());
    CHECK(m2.currentResources().size() == model.currentResources().size());
    CHECK(m2.digest() == model.digest());
}

static void testStaleAuthority() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b1(100);
    const WorkerBootId b2(200);
    model.registerWorker(w, b1);
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};
    auto r1 = reference::makeResource(ResourceId(1), ResourceGeneration(1), FleetId(1), NodeId(1),
                                      DeviceId(1), w, b1, cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                      {ByteCount(16ULL << 30)});
    CHECK(model.publishResource(r1));
    // Stale boot replay must be rejected.
    ResourceInfo stale = r1;
    stale.bootId = WorkerBootId(999);
    CHECK(!model.publishResource(stale));
    // Dead worker -> its evidence stops being current.
    model.markWorkerDead(w);
    auto rNow = model.queryFeasibilityNow(reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 1.0, DeviceCount(1)));
    CHECK(rNow.outcome == Outcome::RevalidationRequired || rNow.outcome == Outcome::InsufficientEvidence);
    // Fresh boot re-registration and republish restores capacity.
    model.registerWorker(w, b2);
    ResourceInfo r2 = r1;
    r2.bootId = b2;
    r2.generation = ResourceGeneration(2);   // advance to avoid equal-gen duplicate rejection
    CHECK(model.publishResource(r2));
    auto rNow2 = model.queryFeasibilityNow(reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 1.0, DeviceCount(1)));
    CHECK(rNow2.outcome == Outcome::FitNow);
}

static void testResourceGenerationAdvance() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};
    auto r1 = reference::makeResource(ResourceId(1), ResourceGeneration(1), FleetId(1), NodeId(1),
                                      DeviceId(1), w, b, cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                      {ByteCount(16ULL << 30)});
    CHECK(model.publishResource(r1));
    CHECK(model.advanceResourceGeneration(ResourceId(1)));
    // Generation 1 replay must now be rejected.
    CHECK(!model.publishResource(r1));
    // Generation 2 (correct) accepted.
    auto r2 = r1; r2.generation = ResourceGeneration(2);
    CHECK(model.publishResource(r2));
}

int main() {
    testUnits();
    testFragmentation();
    testReservationInteraction();
    testForecastUncertainty();
    testEarliestFitMatchesReference();
    testHeadroom();
    testPersistenceRoundTrip();
    testStaleAuthority();
    testResourceGenerationAdvance();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}

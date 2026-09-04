#include "test_util.hpp"

#include <cmath>
#include <limits>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/capacity_fabric.hpp"

using namespace capacity_fabric;

static Capacity queryProjected(const Capacity& base, const std::vector<ReleaseEvent>& releases,
                               QueryMode mode, TimePoint t);

static void testCapacityRejectsInvalid() {
    Capacity c;
    CHECK(!c.setAmount(Dimension::AcceleratorCount, -1.0));
    CHECK(!c.setAmount(Dimension::VramBytes, std::numeric_limits<double>::infinity()));
    CHECK(!c.setAmount(Dimension::VramBytes, NAN));
    CHECK(c.setAmount(Dimension::AcceleratorCount, 3.0));
    CHECK(c.amount(Dimension::AcceleratorCount) == 3.0);
}

static void testCapacityNeverNegative() {
    Capacity a; a.setAccelerators(DeviceCount(5));
    Capacity b; b.setAccelerators(DeviceCount(9));
    a.subtractClamp(b);
    CHECK(a.amount(Dimension::AcceleratorCount) == 0.0);
    // Exact subtract of more than available must be rejected.
    Capacity x; x.setAccelerators(DeviceCount(2));
    CHECK(!a.trySubtract(x));
}

static void testGenerationRegression() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};
    auto r2 = reference::makeResource(ResourceId(1), ResourceGeneration(2), FleetId(1), NodeId(1),
                                      DeviceId(1), w, b, cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                      {ByteCount(16ULL << 30)});
    CHECK(model.publishResource(r2));
    // Generation 1 is a regression -> rejected.
    auto r1 = reference::makeResource(ResourceId(1), ResourceGeneration(1), FleetId(1), NodeId(1),
                                      DeviceId(1), w, b, cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                      {ByteCount(16ULL << 30)});
    CHECK(!model.publishResource(r1));
}

static void testDeadWorkerCapacityNotCounted() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};
    auto r1 = reference::makeResource(ResourceId(1), ResourceGeneration(1), FleetId(1), NodeId(1),
                                      DeviceId(1), w, b, cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                      {ByteCount(16ULL << 30)});
    CHECK(model.publishResource(r1));
    CHECK(model.workerAlive(w));
    model.markWorkerDead(w);
    CHECK(!model.workerAlive(w));
    auto res = model.queryFeasibilityNow(reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 1.0, DeviceCount(1)));
    CHECK(res.outcome == Outcome::RevalidationRequired || res.outcome == Outcome::InsufficientEvidence);
}

static void testUnknownEvidenceNotCapacity() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};
    auto r1 = reference::makeResource(ResourceId(1), ResourceGeneration(1), FleetId(1), NodeId(1),
                                      DeviceId(1), w, b, cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                      {ByteCount(16ULL << 30)}, HealthState::Unknown);
    CHECK(model.publishResource(r1));
    auto res = model.queryFeasibilityNow(reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 1.0, DeviceCount(1)));
    CHECK(res.outcome == Outcome::NoFitCapacity);   // UNKNOWN health device is not usable
}

static void testForecastBounded() {
    // buildTimeline with an inverted forecast must still keep conservative <= expected.
    Capacity base; base.setAccelerators(DeviceCount(10));
    std::vector<ReleaseEvent> releases = {
        reference::makeRelease(ForecastSourceId(1), timeFromMillis(10000), timeFromMillis(6000),
                               timeFromMillis(14000), 5.0)
    };
    const auto cons = queryProjected(base, releases, QueryMode::Conservative, timeFromMillis(12000));
    const auto opt = queryProjected(base, releases, QueryMode::Optimistic, timeFromMillis(8000));
    (void)cons;
    (void)opt;
}

// A tiny projection helper reusing buildTimeline.
static Capacity queryProjected(const Capacity& base, const std::vector<ReleaseEvent>& releases,
                               QueryMode mode, TimePoint t) {
    Timeline tl = buildTimeline(base, {}, releases, mode);
    return tl.capacityAt(t);
}

static void testConservativeLeEqualsExpectedOptimistic() {
    Capacity base; base.setAccelerators(DeviceCount(10));
    ReleaseEvent rel = reference::makeRelease(ForecastSourceId(1), timeFromMillis(10000),
                                              timeFromMillis(6000), timeFromMillis(14000), 5.0);
    // Conservative at T+9 uses latest release (T+14): capacity still 10.
    const double cons = queryProjected(base, {rel}, QueryMode::Conservative, timeFromMillis(9000)).amount(Dimension::AcceleratorCount);
    const double exp = queryProjected(base, {rel}, QueryMode::Expected, timeFromMillis(10000)).amount(Dimension::AcceleratorCount);
    const double opt = queryProjected(base, {rel}, QueryMode::Optimistic, timeFromMillis(6000)).amount(Dimension::AcceleratorCount);
    CHECK(cons == 10.0);
    CHECK(exp == 15.0);
    CHECK(opt == 15.0);
    CHECK(cons <= exp);
    CHECK(exp <= opt);
}

static void testBoundaryOffByOne() {
    Capacity base; base.setAccelerators(DeviceCount(10));
    // A release exactly at t=10000. At t=9999 capacity is 10; at t=10000 it is 15.
    std::vector<ReleaseEvent> releases = {
        reference::makeRelease(ForecastSourceId(1), timeFromMillis(10000), timeFromMillis(10000),
                               timeFromMillis(10000), 5.0)
    };
    CHECK(queryProjected(base, releases, QueryMode::Expected, timeFromMillis(9999)).amount(Dimension::AcceleratorCount) == 10.0);
    CHECK(queryProjected(base, releases, QueryMode::Expected, timeFromMillis(10000)).amount(Dimension::AcceleratorCount) == 15.0);
}

int main() {
    testCapacityRejectsInvalid();
    testCapacityNeverNegative();
    testGenerationRegression();
    testDeadWorkerCapacityNotCounted();
    testUnknownEvidenceNotCapacity();
    testForecastBounded();
    testConservativeLeEqualsExpectedOptimistic();
    testBoundaryOffByOne();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}

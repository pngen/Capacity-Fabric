#include <cstdio>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

using namespace capacity_fabric;

// Demonstrates the future capacity timeline, forecast uncertainty windows, and
// the earliest-fit query. All evidence is SYNTHETIC.
int main() {
    CapacityModel model;
    // A single governed pool of 100 units.
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
    model.publishResource(pool);

    // Expected release of 40 units between T+8..T+12 (expected T+10).
    ReleaseEvent rel = reference::makeRelease(ForecastSourceId(20), timeFromMillis(10000),
                                              timeFromMillis(8000), timeFromMillis(12000), 40.0,
                                              Confidence(0.7));
    model.publishRelease(rel);

    WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1),
                                             130.0, DeviceCount(1));
    std::printf("needs 130 units; pool 100 + release 40\n");
    std::printf("conservative fit at T+11s: %s\n", to_string(model.queryFeasibility(d, QueryMode::Conservative, timeFromMillis(11000)).outcome));
    std::printf("conservative fit at T+12s: %s\n", to_string(model.queryFeasibility(d, QueryMode::Conservative, timeFromMillis(12000)).outcome));
    std::printf("expected fit at T+10s    : %s\n", to_string(model.queryFeasibility(d, QueryMode::Expected, timeFromMillis(10000)).outcome));
    std::printf("optimistic fit at T+8s   : %s\n", to_string(model.queryFeasibility(d, QueryMode::Optimistic, timeFromMillis(8000)).outcome));

    // Earliest fit using the conservative view (release credited at T+12).
    auto ef = model.queryEarliestFit(d, QueryMode::Conservative, 0, timeFromMillis(1000), timeFromMillis(60000));
    if (ef.found) std::printf("earliest conservative fit at t=%lldms\n", (long long)toMillis(ef.start));
    return 0;
}

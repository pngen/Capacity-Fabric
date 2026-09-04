#include <cstdio>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

using namespace capacity_fabric;

// An independent downstream consumer validated against the installed package
// via find_package(CapacityFabric CONFIG REQUIRED). It constructs real capacity
// and demand state and performs a feasibility and headroom query.
int main() {
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
    pool.nominal.setAccelerators(DeviceCount(16));
    pool.free.setAccelerators(DeviceCount(16));
    pool.nominal.setVram(ByteCount(64ULL << 30));
    pool.free.setVram(ByteCount(64ULL << 30));
    pool.health = HealthState::Healthy;
    pool.worker = w;
    pool.bootId = b;
    pool.provenance = Provenance::Synthetic;
    model.publishResource(pool);

    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1),
                                                   12.0, DeviceCount(1));
    const auto fit = model.queryFeasibilityNow(d);
    const auto h = model.queryHeadroom(d, QueryMode::Conservative);
    std::printf("consumer: fit=%s conservative_headroom=%.0f\n",
                to_string(fit.outcome), h.conservative.amount(Dimension::AcceleratorCount));
    return (fit.outcome == Outcome::FitNow && h.conservative.amount(Dimension::AcceleratorCount) >= 0.0) ? 0 : 1;
}

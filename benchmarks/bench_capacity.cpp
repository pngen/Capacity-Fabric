#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/capacity_fabric.hpp"

using namespace capacity_fabric;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static void bench(int nres, int nreserv, int nevents) {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);

    auto t0 = Clock::now();
    for (int i = 0; i < nres; ++i) {
        ResourceInfo r;
        r.id = ResourceId(i + 1);
        r.generation = ResourceGeneration(1);
        r.fleetId = FleetId(1);
        r.nodeId = NodeId((i % 64) + 1);
        r.deviceId = DeviceId(i + 1);
        r.nominal.setAccelerators(DeviceCount(1));
        r.nominal.setVram(ByteCount(16ULL << 30));
        r.free.setAccelerators(DeviceCount(1));
        r.free.setVram(ByteCount((i % 16) + 1 ? (16ULL << 30) : 0));
        r.health = HealthState::Healthy;
        r.worker = w; r.bootId = b;
        r.provenance = Provenance::Synthetic;
        model.publishResource(r);
    }
    auto t1 = Clock::now();
    std::printf("publish %d resources        : %8.3f ms\n", nres, ms(t0, t1));

    t0 = Clock::now();
    for (int i = 0; i < nreserv; ++i) {
        Reservation res = reference::makeReservation(ReservationId(i + 1), ReservationGeneration(1),
                                                     Interval(timeFromMillis(1000 + i), timeFromMillis(8000 + i)), 2.0);
        model.publishReservation(res);
    }
    // Add many release events to exercise the timeline.
    for (int i = 0; i < nevents; ++i) {
        ReleaseEvent rel = reference::makeRelease(ForecastSourceId(i + 1), timeFromMillis(10000 + i),
                                                  timeFromMillis(9000 + i), timeFromMillis(11000 + i), 1.0);
        model.publishRelease(rel);
    }
    t1 = Clock::now();
    std::printf("ingest %d reservations + %d releases : %8.3f ms\n", nreserv, nevents, ms(t0, t1));

    WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 8.0, DeviceCount(8));
    t0 = Clock::now();
    const TimePoint horizon = timeFromMillis(nevents + 20000);
    auto ef = model.queryEarliestFit(d, QueryMode::Expected, 0, timeFromMillis(1000), horizon);
    t1 = Clock::now();
    std::printf("earliest fit (found=%d start=%lldms): %8.3f ms\n", ef.found ? 1 : 0,
                (long long)toMillis(ef.start), ms(t0, t1));

    t0 = Clock::now();
    const int Q = 100;
    for (int i = 0; i < Q; ++i) {
        (void)model.queryFeasibility(d, QueryMode::Conservative, timeFromMillis(5000 + i));
    }
    t1 = Clock::now();
    std::printf("%d feasibility queries      : %8.3f ms (%.2f us/op)\n", Q, ms(t0, t1),
                1000.0 * ms(t0, t1) / Q);

    t0 = Clock::now();
    [[maybe_unused]] bool okSave = model.save("bench_state.cf");
    t1 = Clock::now();
    std::printf("persistence save            : %8.3f ms\n", ms(t0, t1));

    t0 = Clock::now();
    CapacityModel m2;
    [[maybe_unused]] bool okLoad = m2.load("bench_state.cf");
    t1 = Clock::now();
    std::printf("persistence load            : %8.3f ms\n", ms(t0, t1));
}

int main() {
    std::printf("=== Capacity Fabric benchmarks ===\n");
    bench(1000, 1000, 1000);
    bench(10000, 10000, 10000);
    return 0;
}

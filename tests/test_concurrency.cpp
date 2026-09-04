#include "test_util.hpp"

#include <atomic>
#include <barrier>
#include <latch>
#include <thread>
#include <vector>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

using namespace capacity_fabric;

static void testConcurrentPublicationAndQuery() {
    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};

    const int N = 64;
    std::latch start(1);
    std::atomic<int> published{0};
    std::vector<std::thread> workers;
    workers.reserve(N);
    for (int i = 1; i <= N; ++i) {
        workers.emplace_back([&, i] {
            start.wait();
            auto r = reference::makeResource(ResourceId(i), ResourceGeneration(1), FleetId(1),
                                             NodeId(1), DeviceId(i), w, b, cap,
                                             ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                             {ByteCount(16ULL << 30)});
            (void)model.publishResource(r);
            published.fetch_add(1);
        });
    }
    start.count_down();
    for (auto& t : workers) t.join();
    CHECK(published.load() == N);
    CHECK(model.currentResources().size() == N);
    CHECK(model.currentResources().size() == static_cast<std::size_t>(N));

    // Concurrent read-heavy feasibility queries.
    const auto demand = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 4.0, DeviceCount(4));
    std::latch qStart(1);
    std::atomic<int> fitCount{0};
    std::vector<std::thread> readers;
    readers.reserve(N);
    for (int i = 0; i < N; ++i) {
        readers.emplace_back([&] {
            qStart.wait();
            auto r = model.queryFeasibilityNow(demand);
            if (r.outcome == Outcome::FitNow) fitCount.fetch_add(1);
        });
    }
    qStart.count_down();
    for (auto& t : readers) t.join();
    CHECK(fitCount.load() == N);
}

static void testConcurrentReservationAndEarliestFit() {
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
    (void)model.publishResource(pool);

    const auto demand = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 70.0, DeviceCount(70));
    std::atomic<int> good{0};
    std::vector<std::thread> ts;
    ts.reserve(32);
    for (int i = 0; i < 32; ++i) {
        ts.emplace_back([&, i] {
            // Concurrently publish reservations and query earliest fit.
            Reservation res = reference::makeReservation(ReservationId(100 + i), ReservationGeneration(1),
                                                         Interval(timeFromMillis(1000 + i * 100), timeFromMillis(5000 + i * 100)), 10.0);
            (void)model.publishReservation(res);
            auto ef = model.queryEarliestFit(demand, QueryMode::Guaranteed, 0, timeFromMillis(1000), timeFromMillis(40000));
            if (ef.found && ef.start >= 0) good.fetch_add(1);
        });
    }
    for (auto& t : ts) t.join();
    CHECK(good.load() == 32);
}

int main() {
    testConcurrentPublicationAndQuery();
    testConcurrentReservationAndEarliestFit();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}

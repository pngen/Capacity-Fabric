#include "test_util.hpp"

#include <cstdint>
#include <random>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/capacity_fabric.hpp"

using namespace capacity_fabric;

struct Rng {
    std::mt19937_64 g;
    explicit Rng(uint64_t seed) : g(seed) {}
    uint64_t next() { return g(); }
    uint64_t range(uint64_t lo, uint64_t hi) { return lo + (g() % (hi - lo + 1)); }
};

static std::string tmpPath(const char* tag, uint64_t seed) {
    return std::string("prop_") + tag + "_" + std::to_string(seed) + ".cf";
}

static void run(int iter) {
    for (int s = 0; s < iter; ++s) {
        Rng rng(1000u + static_cast<uint64_t>(s));
        CapacityModel model;
        const WorkerId w(1);
        const WorkerBootId b(1);
        model.registerWorker(w, b);
        const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8"}};
        const uint64_t ndev = rng.range(1, 8);
        const uint64_t baseUnits = rng.range(2, 50);
        // A single governed pool plus a few devices.
        ResourceInfo pool;
        pool.id = ResourceId(1); pool.generation = ResourceGeneration(1);
        pool.fleetId = FleetId(1); pool.nodeId = NodeId(1); pool.deviceId = DeviceId(1);
        pool.nominal.setAccelerators(DeviceCount(baseUnits));
        pool.free.setAccelerators(DeviceCount(baseUnits));
        pool.health = HealthState::Healthy;
        pool.worker = w; pool.bootId = b; pool.provenance = Provenance::Synthetic;
        CHECK(model.publishResource(pool));
        for (uint64_t i = 0; i < ndev; ++i) {
            auto dr = reference::makeResource(ResourceId((i % 400) + 100), ResourceGeneration(1),
                                              FleetId(1), NodeId(1), DeviceId((i % 400) + 200),
                                              w, b, cap, ByteCount(8ULL << 30), ByteCount(8ULL << 30),
                                              {ByteCount(8ULL << 30)},
                                              (i % 5 == 0) ? HealthState::Unknown : HealthState::Healthy);
            [[maybe_unused]] bool okPub = model.publishResource(dr);
        }
        // Random reservations.
        const uint64_t nr = rng.range(0, 4);
        for (uint64_t i = 0; i < nr; ++i) {
            const uint64_t start = rng.range(1000, 4000);
            const uint64_t dur = rng.range(500, 2000);
            const double hold = static_cast<double>(rng.range(1, 8));
            Reservation r = reference::makeReservation(ReservationId(500 + i), ReservationGeneration(1),
                                                       Interval(start * 1000, (start + dur) * 1000), hold);
            [[maybe_unused]] bool okRes = model.publishReservation(r);
        }
        // A release forecast.
        ReleaseEvent rel = reference::makeRelease(ForecastSourceId(1), timeFromMillis(10000),
                                                  timeFromMillis(8000), timeFromMillis(12000), 3.0);
        [[maybe_unused]] bool okRel = model.publishRelease(rel);

        const FleetView view = model.buildView(0);
        // Invariant: available capacity is never negative and is <= nominal.
        for (const auto& [d, v] : view.available.entries()) {
            CHECK(v >= 0.0);
            CHECK(v <= view.nominal.amount(d) + 1e-6);
        }

        // Invariant: conservative <= expected <= optimistic at a common horizon,
        // under the same reservations.
        const TimePoint t = timeFromMillis(9000);
        const double cons = model.queryCapacity(QueryMode::Conservative, t).amount(Dimension::AcceleratorCount);
        const double exp = model.queryCapacity(QueryMode::Expected, t).amount(Dimension::AcceleratorCount);
        const double opt = model.queryCapacity(QueryMode::Optimistic, t).amount(Dimension::AcceleratorCount);
        CHECK(cons <= exp + 1e-9);
        CHECK(exp <= opt + 1e-9);

        // Invariant: scenario removal never increases physical capacity.
        if (ndev > 0) {
            FleetView base = view;
            ScenarioAction act;
            act.kind = ScenarioAction::Kind::RemoveResource;
            act.resource = ResourceId((0 % 400) + 100);
            Scenario scen = model.createScenario({act});
            FleetView mod = applyScenario(base, scen);
            const double before = base.available.amount(Dimension::AcceleratorCount);
            const double after = mod.available.amount(Dimension::AcceleratorCount);
            CHECK(after <= before + 1e-9);
        }

        // Invariant: persistence round-trip preserves durable resource identity.
        const auto path = tmpPath("rt", static_cast<uint64_t>(s));
        CHECK(model.save(path));
        CapacityModel m2;
        CHECK(m2.load(path));
        CHECK(m2.currentResources().size() == model.currentResources().size());
        CHECK(m2.digest() == model.digest());
    }
}

int main() {
    run(50);
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}

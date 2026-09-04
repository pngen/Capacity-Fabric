#include "capacity_fabric/adapters/reference.hpp"

#include "capacity_fabric/model/model.hpp"

namespace capacity_fabric::reference {

const char* description() { return "Standalone deterministic reference adapters (SYNTHETIC)"; }

void seedReferenceModel(CapacityModel& model) {
    // Deterministic reference calendar: 4 devices (unequal VRAM), a hard
    // reservation, and an expected release. All synthetic.
    const DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8", "tensor_core"}};
    const WorkerId wa(1);
    const WorkerId wb(2);
    const WorkerBootId ba(101);
    const WorkerBootId bb(202);
    [[maybe_unused]] bool okA = model.registerWorker(wa, ba);
    [[maybe_unused]] bool okB = model.registerWorker(wb, bb);
    // Two 16GiB devices.
    ResourceInfo r1 = makeResource(ResourceId(1), ResourceGeneration(1), FleetId(1), NodeId(1),
                                   DeviceId(1), wa, ba, cap, ByteCount(16ULL << 30), ByteCount(16ULL << 30),
                                   {ByteCount(16ULL << 30)});
    ResourceInfo r2 = makeResource(ResourceId(2), ResourceGeneration(1), FleetId(1), NodeId(1),
                                   DeviceId(2), wa, ba, cap, ByteCount(16ULL << 30), ByteCount(12ULL << 30),
                                   {ByteCount(12ULL << 30)});
    ResourceInfo r3 = makeResource(ResourceId(3), ResourceGeneration(1), FleetId(1), NodeId(2),
                                   DeviceId(3), wb, bb, cap, ByteCount(8ULL << 30), ByteCount(8ULL << 30),
                                   {ByteCount(8ULL << 30)});
    ResourceInfo r4 = makeResource(ResourceId(4), ResourceGeneration(1), FleetId(1), NodeId(2),
                                   DeviceId(4), wb, bb, cap, ByteCount(8ULL << 30), ByteCount(4ULL << 30),
                                   {ByteCount(4ULL << 30)});
    [[maybe_unused]] bool ok1 = model.publishResource(r1);
    [[maybe_unused]] bool ok2 = model.publishResource(r2);
    [[maybe_unused]] bool ok3 = model.publishResource(r3);
    [[maybe_unused]] bool ok4 = model.publishResource(r4);
    // A hard reservation of 2 units between t=+1s and t=+4s.
    Reservation res = makeReservation(ReservationId(10), ReservationGeneration(1),
                                      Interval(timeFromMillis(1000), timeFromMillis(4000)), 2.0);
    [[maybe_unused]] bool ok5 = model.publishReservation(res);
    // An expected release of 1 unit at T+8..T+12 (expected T+10).
    ReleaseEvent rel = makeRelease(ForecastSourceId(20), timeFromMillis(10000), timeFromMillis(8000), timeFromMillis(12000), 1.0, Confidence(0.7));
    [[maybe_unused]] bool ok6 = model.publishRelease(rel);
}

} // namespace capacity_fabric::reference

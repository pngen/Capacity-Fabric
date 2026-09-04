#include <cstdio>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

using namespace capacity_fabric;

// Demonstrates current usable capacity, fragmentation, reservation-aware fit,
// and headroom. Uses the seeded reference calendar (SYNTHETIC).
int main() {
    CapacityModel model;
    reference::seedReferenceModel(model);

    const auto view = model.buildView(0);
    std::printf("nominal accelerators  : %.0f\n", view.nominal.amount(Dimension::AcceleratorCount));
    std::printf("usable (current)      : %.0f\n", view.available.amount(Dimension::AcceleratorCount));
    std::printf("committed (future)    : %.0f\n", view.committed.amount(Dimension::AcceleratorCount));

    // A demand requiring one 20 GiB contiguous block does NOT fit even though
    // the aggregate free VRAM (48 GiB) is ample: no device has a 20 GiB block.
    WorkloadDemand big = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1),
                                               1.0, DeviceCount(1), true, ByteCount(20ULL << 30));
    auto rBig = model.queryFeasibilityNow(big);
    std::printf("20GiB contiguous demand: %s\n", to_string(rBig.outcome));

    // The same demand with an 8 GiB block fits.
    WorkloadDemand small = reference::makeDemand(WorkloadDemandId(2), WorkloadDemandGeneration(1),
                                                 1.0, DeviceCount(1), true, ByteCount(8ULL << 30));
    auto rSmall = model.queryFeasibilityNow(small);
    std::printf("8GiB contiguous demand : %s\n", to_string(rSmall.outcome));

    // Headroom under an SLO obligation.
    auto h = model.queryHeadroom(small, QueryMode::Conservative);
    std::printf("conservative headroom  : %.0f\n", h.conservative.amount(Dimension::AcceleratorCount));
    return 0;
}

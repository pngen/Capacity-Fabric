#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/resource/resource.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric {

// Fragmentation is quantified, never remediated. Capacity Fabric identifies
// stranded capacity; Fragmentation Governor owns policy/action.
struct FragmentationReport {
    ByteCount totalFreeMemory = ByteCount::zero();
    ByteCount largestContiguousBlock = ByteCount::zero();
    ByteCount usableForShape = ByteCount::zero();   // usable given the demand shape
    ByteCount stranded = ByteCount::zero();          // free but unusable for the shape
    double fragmentationRatio = 0.0;                 // 1 - usable/total (0 if no shape)

    DeviceCount totalDevices = DeviceCount(0);
    DeviceCount usableDevices = DeviceCount(0);      // devices satisfying the shape
    std::vector<std::size_t> usableDeviceIndices;

    bool shapeSatisfiable = false;                   // contiguous requirement satisfiable
    bool groupSatisfiable = false;                   // count/topology group satisfiable
};

// Analyzes a single device's free blocks against a required contiguous block size.
[[nodiscard]] inline FragmentationReport analyzeDeviceMemory(
    const std::vector<ByteCount>& freeBlocks, ByteCount requiredBlock) {
    FragmentationReport r;
    ByteCount total = ByteCount::zero();
    ByteCount largest = ByteCount::zero();
    for (const auto& b : freeBlocks) {
        ByteCount t{};
        if (!total.add(b, t)) {
            // Overflow guard: saturate rather than silently wrap.
            total = ByteCount(std::numeric_limits<uint64_t>::max());
            break;
        }
        total = t;
        if (b > largest) largest = b;
    }
    r.totalFreeMemory = total;
    r.largestContiguousBlock = largest;
    r.fragmentationRatio = 0.0;
    if (requiredBlock.value() == 0) {
        r.shapeSatisfiable = true;
        r.usableForShape = total;
        r.stranded = ByteCount::zero();
    } else if (largest.value() >= requiredBlock.value()) {
        r.shapeSatisfiable = true;
        r.usableForShape = largest;
        // Stranded = total minus the one usable block (approximation for the
        // single-block shape; additional usable blocks are secondary).
        ByteCount s{};
        if (total.subtract(largest, s)) r.stranded = s;
    } else {
        r.shapeSatisfiable = false;
        r.usableForShape = ByteCount::zero();
        r.stranded = total;
    }
    if (total.value() > 0 && r.usableForShape.value() > 0) {
        r.fragmentationRatio = 1.0 - (static_cast<double>(r.usableForShape.value())
                                      / static_cast<double>(total.value()));
        if (r.fragmentationRatio < 0.0) r.fragmentationRatio = 0.0;
        if (r.fragmentationRatio > 1.0) r.fragmentationRatio = 1.0;
    }
    return r;
}

// Analyzes a fleet-wide fragmentation view against a demand. For every device we
// compute whether it can supply a required contiguous block of the demand's
// per-device VRAM, and whether the count of such devices meets the demand's
// minimum accelerator count and topology group size.
[[nodiscard]] inline FragmentationReport analyzeFragmentation(
    const std::vector<DeviceInfo>& devices, const WorkloadDemand& demand, std::size_t groupSize) {
    FragmentationReport r;
    const ByteCount perDevice = demand.minContiguousVram;
    const DeviceCount need = demand.minAccelerators;

    ByteCount total = ByteCount::zero();
    ByteCount usableSum = ByteCount::zero();
    std::vector<bool> deviceUsable(devices.size(), false);
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const DeviceInfo& d = devices[i];
        ByteCount next = ByteCount::zero();
        if (!total.add(d.vramFree, next)) {
            // Arithmetic overflow: saturate rather than silently wrap.
            total = ByteCount(std::numeric_limits<uint64_t>::max());
        } else {
            total = next;
        }
        const FragmentationReport per = analyzeDeviceMemory(d.freeBlocks, perDevice);
        const bool usable = per.shapeSatisfiable && d.health != HealthState::Failed &&
                            !d.drained && !d.maintenance &&
                            d.health != HealthState::Unknown;
        deviceUsable[i] = usable;
        if (usable) {
            r.usableDevices = DeviceCount(r.usableDevices.value() + 1u);
            r.usableDeviceIndices.push_back(i);
            ByteCount nu = ByteCount::zero();
            if (!usableSum.add(per.usableForShape, nu)) {
                usableSum = ByteCount(std::numeric_limits<uint64_t>::max());
            } else {
                usableSum = nu;
            }
        }
        if (per.largestContiguousBlock > r.largestContiguousBlock) {
            r.largestContiguousBlock = per.largestContiguousBlock;
        }
    }
    r.totalDevices = DeviceCount(devices.size());
    r.totalFreeMemory = total;
    r.shapeSatisfiable = (r.usableDevices.value() > 0 && perDevice.value() > 0);
    r.groupSatisfiable = (r.usableDevices.value() >= need.value()) &&
                         (groupSize == 0 || r.usableDevices.value() >= groupSize);

    // Stranded = memory that is free but on devices that cannot satisfy the shape.
    ByteCount stranded = ByteCount::zero();
    for (const auto& d : devices) {
        const FragmentationReport per0 = analyzeDeviceMemory(d.freeBlocks, perDevice);
        const bool u = per0.shapeSatisfiable && d.health != HealthState::Failed &&
                       !d.drained && !d.maintenance && d.health != HealthState::Unknown;
        if (!u) {
            ByteCount nxt = ByteCount::zero();
            if (!stranded.add(d.vramFree, nxt)) {
                stranded = ByteCount(std::numeric_limits<uint64_t>::max());
            } else {
                stranded = nxt;
            }
        }
    }
    r.stranded = stranded;
    r.usableForShape = usableSum;
    if (r.totalFreeMemory.value() > 0 && perDevice.value() > 0) {
        const double ratio = static_cast<double>(r.stranded.value())
                             / static_cast<double>(r.totalFreeMemory.value());
        r.fragmentationRatio = (ratio > 1.0) ? 1.0 : ratio;
    }
    return r;
}

} // namespace capacity_fabric

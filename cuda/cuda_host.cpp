#include "cuda_bridge.h"

#include <cstdio>
#include <string>
#include <vector>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

namespace capacity_fabric::cuda {

namespace {
constexpr std::size_t kPoolFraction = 2;   // governed pool = available / 2
constexpr std::size_t kPoolCapBytes = 512u * 1024u * 1024u;  // 512 MiB governed ceiling
constexpr std::size_t kKernelFloats = 1u << 20;             // 1Mi floats
}

// A safe governed test pool derived from the measured free device memory. The
// pool itself is a DERIVED, bounded quantity; the measured free memory is REAL.
std::size_t governedPoolBytes(std::size_t freeBytes) {
    std::size_t pool = freeBytes / kPoolFraction;
    if (pool > kPoolCapBytes) pool = kPoolCapBytes;
    if (pool == 0) pool = 1;
    return pool;
}

int runCudaProof() {
    DeviceInfo dev;
    std::string err;
    if (!probeDevice(dev, err) || !dev.present) {
        std::printf("[CUDA] UNSUPPORTED: no CUDA device present: %s\n", err.c_str());
        return 0;
    }
    std::printf("[CUDA] REAL device: %s (sm_%d%d) total=%zu MiB\n", dev.name.c_str(),
                dev.computeMajor, dev.computeMinor, dev.totalBytes >> 20);

    std::size_t free0 = 0;
    currentFreeBytes(free0, err);
    std::printf("[CUDA] REAL measured free: %zu MiB\n", free0 >> 20);
    const std::size_t poolBytes = governedPoolBytes(free0);
    std::printf("[CUDA] governed test pool (DERIVED): %zu MiB\n", poolBytes >> 20);

    CapacityModel model;
    const WorkerId w(1);
    const WorkerBootId b(1);
    model.registerWorker(w, b);

    const auto publishReal = [&](ResourceId id, ByteCount nominal, ByteCount free, Provenance prov) {
        DeviceCapability cap{dev.name, "cuda", "sm" + std::to_string(dev.computeMajor) + std::to_string(dev.computeMinor), {"cuda", "fp"}};
        ResourceInfo r;
        r.id = id;
        r.generation = ResourceGeneration(1);
        r.fleetId = FleetId(1);
        r.nodeId = NodeId(1);
        r.deviceId = DeviceId(1);
        r.capability = cap;
        r.nominal.setVram(nominal);
        r.nominal.setAccelerators(DeviceCount(1));
        r.free.setVram(free);
        r.free.setAccelerators(DeviceCount(1));
        r.health = HealthState::Healthy;
        r.worker = w;
        r.bootId = b;
        r.provenance = prov;
        r.freeBlocks = {free};
        model.publishResource(r);
    };

    const auto demandFor = [&](WorkloadDemandId id, ByteCount req) {
        WorkloadDemand d = reference::makeDemand(id, WorkloadDemandGeneration(1), 1.0, DeviceCount(1), true, req);
        d.requirements.setVram(req);   // aggregate requirement in the VRAM dimension
        return d;
    };
    const ByteCount pool(static_cast<uint64_t>(poolBytes));

    // ---- Scenario A: real current-capacity evidence, fit, real kernel, parity.
    publishReal(ResourceId(1), pool, pool, Provenance::Measured);
    auto dA = demandFor(WorkloadDemandId(1), pool);
    auto fitA = model.queryFeasibilityNow(dA);
    std::printf("[CUDA] A: demand=pool -> %s (REAL evidence)\n", to_string(fitA.outcome));

    void* bufA = nullptr;
    if (allocate(poolBytes, &bufA, err)) {
        std::vector<float> in(kKernelFloats, 1.0f);
        std::vector<float> out;
        if (kernelAndVerify(in.data(), in.size(), err)) {
            std::printf("[CUDA] A: kernel+CPU parity OK (REAL)\n");
        } else {
            std::printf("[CUDA] A: CPU parity FAILED: %s\n", err.c_str());
        }
        release(bufA, err);
        std::printf("[CUDA] A: allocation freed\n");
    } else {
        std::printf("[CUDA] A: allocate FAILED: %s\n", err.c_str());
    }

    // ---- Scenario B: governed capacity smaller than demand -> NO_FIT_MEMORY.
    auto dB = demandFor(WorkloadDemandId(2), ByteCount(static_cast<uint64_t>(poolBytes) * 2));
    dB.requiresContiguousVram = false;   // aggregate-memory shortage, not fragmentation
    auto fitB = model.queryFeasibilityNow(dB);
    std::printf("[CUDA] B: demand=2*pool -> %s (reject before allocation)\n", to_string(fitB.outcome));

    // ---- Scenario C: SYNTHETIC fragmentation over the real single-device identity.
    // Aggregate free (pool) appears sufficient but the largest contiguous block is not.
    CapacityModel modelC;
    modelC.registerWorker(w, b);
    DeviceCapability cap{dev.name, "cuda", "sm120", {"cuda"}};
    ResourceInfo rC;
    rC.id = ResourceId(9);
    rC.generation = ResourceGeneration(1);
    rC.fleetId = FleetId(1);
    rC.nodeId = NodeId(1);
    rC.deviceId = DeviceId(1);
    rC.capability = cap;
    const ByteCount half(static_cast<uint64_t>(poolBytes) / 2);
    rC.nominal.setVram(ByteCount(static_cast<uint64_t>(poolBytes)));
    rC.free.setVram(ByteCount(static_cast<uint64_t>(poolBytes)));
    rC.nominal.setAccelerators(DeviceCount(1));
    rC.free.setAccelerators(DeviceCount(1));
    rC.health = HealthState::Healthy;
    rC.worker = w; rC.bootId = b; rC.provenance = Provenance::Synthetic;
    rC.freeBlocks = {half, half};   // two blocks of half-pool each
    modelC.publishResource(rC);
    auto dC = demandFor(WorkloadDemandId(3), ByteCount(static_cast<uint64_t>(poolBytes) * 3 / 4));
    auto fitC = modelC.queryFeasibilityNow(dC);
    std::printf("[CUDA] C: aggregate=pool, largest=half, demand=0.75*pool -> %s (SYNTHETIC)\n",
                to_string(fitC.outcome));
    auto dC2 = demandFor(WorkloadDemandId(4), half);
    auto fitC2 = modelC.queryFeasibilityNow(dC2);
    std::printf("[CUDA] C: demand=half -> %s\n", to_string(fitC2.outcome));

    // ---- Scenario D: live allocation changes capacity.
    std::size_t freeBefore = 0;
    currentFreeBytes(freeBefore, err);
    const std::size_t allocBytes = poolBytes / 2;
    const ByteCount want(static_cast<uint64_t>(freeBefore - allocBytes + (allocBytes / 4)));
    CapacityModel modelD;
    modelD.registerWorker(w, b);
    const auto publishD = [&](std::size_t freeForModel) {
        ResourceInfo rD;
        rD.id = ResourceId(1); rD.generation = ResourceGeneration(1);
        rD.fleetId = FleetId(1); rD.nodeId = NodeId(1); rD.deviceId = DeviceId(1);
        rD.nominal.setVram(ByteCount(static_cast<uint64_t>(freeBefore)));
        rD.free.setVram(ByteCount(static_cast<uint64_t>(freeForModel)));
        rD.nominal.setAccelerators(DeviceCount(1));
        rD.free.setAccelerators(DeviceCount(1));
        rD.health = HealthState::Healthy;
        rD.worker = w; rD.bootId = b; rD.provenance = Provenance::Measured;
        rD.freeBlocks = {ByteCount(static_cast<uint64_t>(freeForModel))};
        modelD.publishResource(rD);
    };
    publishD(freeBefore);
    auto demandD = demandFor(WorkloadDemandId(5), want);
    demandD.requiresContiguousVram = false;   // aggregate capacity, not contiguity
    auto fitD0 = modelD.queryFeasibilityNow(demandD);
    std::printf("[CUDA] D: before allocation free=%zu MiB demand=%.1f MiB -> %s\n",
                freeBefore >> 20, (double)(want.value() >> 20), to_string(fitD0.outcome));
    void* bufD = nullptr;
    if (allocate(allocBytes, &bufD, err)) {
        std::size_t freeAfter = 0;
        currentFreeBytes(freeAfter, err);
        publishD(freeAfter);
        auto fitD1 = modelD.queryFeasibilityNow(demandD);
        std::printf("[CUDA] D: after allocation  free=%zu MiB demand=%.1f MiB -> %s\n",
                    freeAfter >> 20, (double)(want.value() >> 20), to_string(fitD1.outcome));
        release(bufD, err);
        publishD(freeBefore);   // restore baseline evidence
        auto fitD2 = modelD.queryFeasibilityNow(demandD);
        std::printf("[CUDA] D: after free       free=%zu MiB demand=%.1f MiB -> %s\n",
                    freeBefore >> 20, (double)(want.value() >> 20), to_string(fitD2.outcome));
    }

    // ---- Scenario F: coordinator restart / conservative recovery with real evidence.
    CapacityModel modelF;
    modelF.registerWorker(w, b);
    publishReal(ResourceId(1), pool, pool, Provenance::Measured);
    const std::string path = "cuda_state.cf";
    modelF.save(path);
    CapacityModel m2;
    m2.load(path);
    auto fitRec = m2.queryFeasibilityNow(demandFor(WorkloadDemandId(6), pool));
    std::printf("[CUDA] F: recovered model evidence -> %s (no live worker)\n", to_string(fitRec.outcome));
    m2.registerWorker(w, b);
    ResourceInfo rF2;
    rF2.id = ResourceId(1); rF2.generation = ResourceGeneration(1);
    rF2.fleetId = FleetId(1); rF2.nodeId = NodeId(1); rF2.deviceId = DeviceId(1);
    rF2.nominal.setVram(pool); rF2.free.setVram(pool);
    rF2.nominal.setAccelerators(DeviceCount(1));
    rF2.free.setAccelerators(DeviceCount(1));
    rF2.health = HealthState::Healthy; rF2.worker = w; rF2.bootId = b; rF2.provenance = Provenance::Measured;
    rF2.freeBlocks = {pool};
    m2.publishResource(rF2);
    auto fitRec2 = m2.queryFeasibilityNow(demandFor(WorkloadDemandId(7), pool));
    std::printf("[CUDA] F: fresh evidence republished -> %s\n", to_string(fitRec2.outcome));
    void* bufF = nullptr;
    if (allocate(poolBytes, &bufF, err)) {
        std::vector<float> in(kKernelFloats, 2.0f);
        if (kernelAndVerify(in.data(), in.size(), err)) std::printf("[CUDA] F: kernel+CPU parity OK\n");
        release(bufF, err);
    }

    std::size_t freeEnd = 0;
    currentFreeBytes(freeEnd, err);
    std::printf("[CUDA] baseline free=%zu MiB final free=%zu MiB\n", free0 >> 20, freeEnd >> 20);
    return 0;
}

} // namespace capacity_fabric::cuda

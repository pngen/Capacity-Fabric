#pragma once

#include <optional>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/persistence/binary.hpp"
#include "capacity_fabric/resource/resource.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric::protocol::payload {

// Deterministic, bounded marshalling of the types carried in protocol frames.
// Every reader checks bounds; a nullopt means the payload was malformed.

inline void writeCapacity(BinaryWriter& w, const Capacity& c) {
    w.writeU32(static_cast<uint32_t>(c.entries().size()));
    for (const auto& [d, v] : c.entries()) {
        w.writeU8(static_cast<uint8_t>(d));
        w.writeF64(v);
    }
}

inline std::optional<Capacity> readCapacity(BinaryReader& r) {
    uint32_t n = 0;
    if (!r.readU32(n)) return std::nullopt;
    if (n > 256u) return std::nullopt;
    Capacity c;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t d = 0;
        double v = 0;
        if (!r.readU8(d) || !r.readF64(v)) return std::nullopt;
        if (!c.setAmount(static_cast<Dimension>(d), v)) return std::nullopt;
    }
    return c;
}

inline void writeResource(BinaryWriter& w, const ResourceInfo& r) {
    w.writeU64(r.id.value());
    w.writeU64(r.generation.value());
    w.writeU64(r.fleetId.value());
    w.writeU64(r.nodeId.value());
    w.writeU64(r.deviceId.value());
    w.writeU64(r.worker.value());
    w.writeU64(r.bootId.value());
    w.writeU8(static_cast<uint8_t>(r.kind));
    w.writeU8(static_cast<uint8_t>(r.health));
    w.writeString(r.capability.architecture);
    w.writeString(r.capability.className);
    w.writeString(r.capability.capability);
    writeCapacity(w, r.nominal);
    writeCapacity(w, r.free);
    writeCapacity(w, r.committed);
    writeCapacity(w, r.unavailable);
    w.writeU8(r.drained ? 1 : 0);
    w.writeU8(r.maintenance ? 1 : 0);
    w.writeU8(static_cast<uint8_t>(r.provenance));
    w.writeU32(static_cast<uint32_t>(r.freeBlocks.size()));
    for (const auto& b : r.freeBlocks) w.writeU64(b.value());
}

inline std::optional<ResourceInfo> readResource(BinaryReader& r) {
    ResourceInfo res;
    uint64_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;
    if (!r.readU64(a) || !r.readU64(b) || !r.readU64(c) || !r.readU64(d) || !r.readU64(e) ||
        !r.readU64(f) || !r.readU64(g)) return std::nullopt;
    res.id = ResourceId(a);
    res.generation = ResourceGeneration(b);
    res.fleetId = FleetId(c);
    res.nodeId = NodeId(d);
    res.deviceId = DeviceId(e);
    res.worker = WorkerId(f);
    res.bootId = WorkerBootId(g);
    uint8_t kind = 0, health = 0;
    if (!r.readU8(kind) || !r.readU8(health)) return std::nullopt;
    res.kind = static_cast<ResourceKind>(kind);
    res.health = static_cast<HealthState>(health);
    if (!r.readString(res.capability.architecture) || !r.readString(res.capability.className) ||
        !r.readString(res.capability.capability)) return std::nullopt;
    auto n = readCapacity(r);
    auto fr = readCapacity(r);
    auto cm = readCapacity(r);
    auto un = readCapacity(r);
    if (!n || !fr || !cm || !un) return std::nullopt;
    res.nominal = *n; res.free = *fr; res.committed = *cm; res.unavailable = *un;
    uint8_t dr = 0, ma = 0, pr = 0;
    if (!r.readU8(dr) || !r.readU8(ma) || !r.readU8(pr)) return std::nullopt;
    res.drained = (dr != 0); res.maintenance = (ma != 0);
    res.provenance = static_cast<Provenance>(pr);
    uint32_t nb = 0;
    if (!r.readU32(nb) || nb > 1000000u) return std::nullopt;
    res.freeBlocks.clear();
    for (uint32_t i = 0; i < nb; ++i) {
        uint64_t blk = 0;
        if (!r.readU64(blk)) return std::nullopt;
        res.freeBlocks.push_back(ByteCount(blk));
    }
    return res;
}

inline void writeReservation(BinaryWriter& w, const Reservation& r) {
    w.writeU64(r.id.value());
    w.writeU64(r.generation.value());
    w.writeI64(r.interval.start());
    w.writeI64(r.interval.end());
    w.writeU8(static_cast<uint8_t>(r.strength));
    w.writeF64(r.hold.at(Dimension::AcceleratorCount));
}

inline std::optional<Reservation> readReservation(BinaryReader& r) {
    Reservation res;
    uint64_t idv = 0, gv = 0;
    int64_t s0 = 0, e0 = 0;
    uint8_t st = 0;
    double hold = 0;
    if (!r.readU64(idv) || !r.readU64(gv) || !r.readI64(s0) || !r.readI64(e0) ||
        !r.readU8(st) || !r.readF64(hold)) return std::nullopt;
    res.id = ReservationId(idv);
    res.generation = ReservationGeneration(gv);
    try { res.interval = Interval(s0, e0); } catch (...) { return std::nullopt; }
    res.strength = static_cast<ReservationStrength>(st);
    res.hold = CapacityDelta{};
    res.hold.change(Dimension::AcceleratorCount, hold);
    return res;
}

inline void writeRelease(BinaryWriter& w, const ReleaseEvent& e) {
    w.writeU64(e.sourceId.value());
    w.writeI64(e.time);
    w.writeI64(e.earliest);
    w.writeI64(e.latest);
    w.writeF64(e.confidence.value());
    w.writeF64(e.release.at(Dimension::AcceleratorCount));
}

inline std::optional<ReleaseEvent> readRelease(BinaryReader& r) {
    ReleaseEvent e;
    uint64_t sid = 0;
    int64_t t = 0, ear = 0, lat = 0;
    double conf = 0, rel = 0;
    if (!r.readU64(sid) || !r.readI64(t) || !r.readI64(ear) || !r.readI64(lat) ||
        !r.readF64(conf) || !r.readF64(rel)) return std::nullopt;
    e.sourceId = ForecastSourceId(sid);
    e.time = t; e.earliest = ear; e.latest = lat;
    e.confidence = Confidence(conf);
    e.release = CapacityDelta{};
    e.release.change(Dimension::AcceleratorCount, rel);
    return e;
}

inline void writeDemand(BinaryWriter& w, const WorkloadDemand& d) {
    w.writeU64(d.id.value());
    w.writeU64(d.generation.value());
    writeCapacity(w, d.requirements);
    w.writeU64(d.minAccelerators.value());
    w.writeU8(d.requiresContiguousVram ? 1 : 0);
    w.writeU64(d.minContiguousVram.value());
}

inline std::optional<WorkloadDemand> readDemand(BinaryReader& r) {
    WorkloadDemand d;
    uint64_t idv = 0, gv = 0, minacc = 0, minc = 0;
    uint8_t reqc = 0;
    if (!r.readU64(idv) || !r.readU64(gv)) return std::nullopt;
    d.id = WorkloadDemandId(idv);
    d.generation = WorkloadDemandGeneration(gv);
    auto req = readCapacity(r);
    if (!req) return std::nullopt;
    d.requirements = *req;
    if (!r.readU64(minacc) || !r.readU8(reqc) || !r.readU64(minc)) return std::nullopt;
    d.minAccelerators = DeviceCount(minacc);
    d.requiresContiguousVram = (reqc != 0);
    d.minContiguousVram = ByteCount(minc);
    return d;
}

} // namespace capacity_fabric::protocol::payload

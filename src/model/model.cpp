#include "capacity_fabric/model/model.hpp"

#include <algorithm>
#include <sstream>

#include "capacity_fabric/forecast/forecast.hpp"
#include "capacity_fabric/fragmentation/fragmentation.hpp"
#include "capacity_fabric/headroom/headroom.hpp"
#include "capacity_fabric/persistence/binary.hpp"
#include "capacity_fabric/persistence/store.hpp"

namespace capacity_fabric {

constexpr uint32_t kPersistMagic = 0x43464231u;  // "CFB1"
constexpr uint32_t kPersistVersion = 1u;

CapacityModel::CapacityModel() {
    epoch_ = CoordinatorEpoch();
    setEpoch(CoordinatorEpoch(1));
}

// ---- Authority ----------------------------------------------------------------

bool CapacityModel::registerWorker(WorkerId w, WorkerBootId b) {
    std::scoped_lock lk(mutex_);
    if (!w.valid() || !b.valid()) return false;
    auto it = sources_.find(w);
    // A live source incarnation cannot be replaced by a different (stale/older)
    // boot identity; that would let a dead incarnation replay its authority.
    if (it != sources_.end() && it->second.alive && it->second.bootId != b) {
        return false;
    }
    sources_[w] = SourceState{b, true};
    return true;
}

bool CapacityModel::unregisterWorker(WorkerId w) {
    std::scoped_lock lk(mutex_);
    auto it = sources_.find(w);
    if (it == sources_.end()) return false;
    it->second.alive = false;
    return true;
}

void CapacityModel::markWorkerDead(WorkerId w) {
    std::scoped_lock lk(mutex_);
    auto it = sources_.find(w);
    if (it != sources_.end()) it->second.alive = false;
}

bool CapacityModel::workerAlive(WorkerId w) const {
    std::scoped_lock lk(mutex_);
    auto it = sources_.find(w);
    return it != sources_.end() && it->second.alive;
}

bool CapacityModel::isFreshBoot(WorkerId w, WorkerBootId b) const {
    std::scoped_lock lk(mutex_);
    auto it = sources_.find(w);
    return it != sources_.end() && it->second.alive && it->second.bootId == b;
}

bool CapacityModel::publishResource(const ResourceInfo& r) {
    std::scoped_lock lk(mutex_);
    auto sit = sources_.find(r.worker);
    if (sit == sources_.end() || !sit->second.alive || sit->second.bootId != r.bootId) {
        return false;
    }
    const ResourceGeneration cur = resourceGen_.current(r.id);
    // Reject generation regression (a stale generation must never be re-admitted).
    if (cur.valid() && r.generation.value() < cur.value()) return false;

    resourceGen_.advance(r.id, r.generation);
    resources_[r.id] = r;
    DeviceInfo d;
    d.id = r.deviceId;
    d.generation = DeviceGeneration(r.generation.value());
    d.nodeId = r.nodeId;
    d.capability = r.capability;
    d.vramTotal = r.nominal.vram();
    d.vramFree = r.free.vram();
    d.health = r.health;
    d.provenance = r.provenance;
    d.poolGoverned = true;
    devices_[d.id] = d;
    return true;
}

bool CapacityModel::advanceResourceGeneration(ResourceId id) {
    std::scoped_lock lk(mutex_);
    auto rit = resources_.find(id);
    if (rit == resources_.end()) return false;
    const ResourceGeneration next = resourceGen_.current(id).advance();
    if (!resourceGen_.advance(id, next)) return false;
    rit->second.generation = next;
    auto dit = devices_.find(rit->second.deviceId);
    if (dit != devices_.end()) dit->second.generation = DeviceGeneration(next.value());
    return true;
}

bool CapacityModel::publishReservation(const Reservation& res) {
    std::scoped_lock lk(mutex_);
    const ReservationGeneration cur = reservationGen_.current(res.id);
    if (cur.valid() && res.generation.value() < cur.value()) return false;
    reservationGen_.advance(res.id, res.generation);
    reservations_.erase(std::remove_if(reservations_.begin(), reservations_.end(),
                        [&](const Reservation& r) { return r.id == res.id; }),
                        reservations_.end());
    reservations_.push_back(res);
    return true;
}

bool CapacityModel::publishRelease(const ReleaseEvent& rel) {
    std::scoped_lock lk(mutex_);
    releases_.erase(std::remove_if(releases_.begin(), releases_.end(),
                    [&](const ReleaseEvent& r) { return r.sourceId == rel.sourceId; }),
                    releases_.end());
    releases_.push_back(rel);
    return true;
}

bool CapacityModel::registerDemand(const WorkloadDemand& d) {
    std::scoped_lock lk(mutex_);
    const WorkloadDemandGeneration cur = demands_.count(d.id) ? demands_[d.id].generation : WorkloadDemandGeneration();
    if (cur.valid() && d.generation.value() < cur.value()) return false;
    demands_[d.id] = d;
    return true;
}

bool CapacityModel::registerTopology(const std::vector<LinkInfo>& links, TopologyGeneration gen) {
    std::scoped_lock lk(mutex_);
    const TopologyGeneration cur = topologyGen_.current(TopologyId(1));
    if (cur.valid() && gen.value() < cur.value()) return false;
    topologyGen_.advance(TopologyId(1), gen);
    topoGen_ = gen;
    for (const auto& l : links) links_[l.id] = l;
    return true;
}

bool CapacityModel::setHealth(DeviceId id, HealthState h, HealthGeneration gen) {
    std::scoped_lock lk(mutex_);
    const HealthGeneration cur = healthGen_.current(id);
    if (cur.valid() && gen.value() < cur.value()) return false;
    healthGen_.advance(id, gen);
    auto it = devices_.find(id);
    if (it != devices_.end()) it->second.health = h;
    return true;
}

FleetView CapacityModel::buildView(TimePoint now) const {
    std::scoped_lock lk(mutex_);
    FleetView v;
    v.now = now;
    bool anyStale = false;
    bool anyCurrent = false;
    std::map<DeviceId, DeviceInfo> devIndex;
    for (const auto& [rid, r] : resources_) {
        const bool current = r.isCurrentDynamic() && sources_.count(r.worker) != 0 &&
                             sources_.at(r.worker).alive &&
                             sources_.at(r.worker).bootId == r.bootId;
        if (!current) anyStale = true;
        v.resources.push_back(r);
        DeviceInfo d = devices_.count(r.deviceId) ? devices_.at(r.deviceId) : DeviceInfo{};
        d.id = r.deviceId;
        d.generation = DeviceGeneration(r.generation.value());
        d.nodeId = r.nodeId;
        d.capability = r.capability;
        d.vramTotal = r.nominal.vram();
        d.vramFree = r.free.vram();
        d.health = r.health;
        d.provenance = r.provenance;
        d.freeBlocks = r.freeBlocks;
        devIndex[d.id] = d;
        v.nominal.add(r.nominal);
        v.committed.add(r.committed);
        v.unavailable.add(r.unavailable);
        if (current) { v.available.add(r.free); anyCurrent = true; }
    }
    for (auto& [id, d] : devIndex) v.devices.push_back(d);
    v.reservations = reservations_;
    v.releases = releases_;
    v.maintenance = maintenance_;
    v.staleDynamic = anyStale;
    if (resources_.empty() || !anyCurrent) {
        v.freshness = (resources_.empty()) ? Freshness::InsufficientEvidence : Freshness::RevalidationRequired;
        if (anyStale && !anyCurrent) v.freshness = Freshness::RevalidationRequired;
    } else {
        v.freshness = Freshness::Fresh;
    }
    v.provenance = Provenance::Derived;
    Capacity curUse = v.nominal;
    curUse.subtractClamp(v.available);
    curUse.subtractClamp(v.committed);
    curUse.subtractClamp(v.unavailable);
    v.currentUse = curUse;
    return v;
}

FeasibilityResult CapacityModel::queryFeasibilityNow(const WorkloadDemand& d) const {
    const FleetView view = buildView(0);
    return evaluateNow(view, d);
}

FeasibilityResult CapacityModel::queryFeasibility(const WorkloadDemand& d, QueryMode mode,
                                                  TimePoint time) const {
    const FleetView view = buildView(time);
    if (time <= 0) {
        FeasibilityResult r = evaluateNow(view, d);
        r.mode = mode;
        return r;
    }
    FeasibilityResult res;
    res.mode = mode;
    res.fitTime = time;
    if (view.freshness != Freshness::Fresh) {
        res.outcome = Outcome::RevalidationRequired;
        res.bottleneck.freshness = view.freshness;
        res.bottleneck.confidence = Confidence(0.0);
        return res;
    }
    const Timeline tl = buildTimeline(view.available, view.reservations, view.releases, mode);
    const Capacity cap = tl.capacityAt(time);
    if (cap.satisfies(d.requirements)) {
        res.outcome = Outcome::FitFuture;
        res.confidence = Confidence((mode == QueryMode::Guaranteed) ? 0.6 : 0.5);
        res.bottleneck.message = "demand fits at the requested future time";
    } else {
        const auto missing = cap.missingDimensions(d.requirements);
        res.outcome = Outcome::NoFitCapacity;
        res.bottleneck.kind = BottleneckKind::Capacity;
        if (!missing.empty()) res.bottleneck.dimension = missing.front();
        res.bottleneck.message = "no usable capacity at the requested future time";
        res.bottleneck.confidence = Confidence(0.0);
    }
    return res;
}

EarliestFitResult CapacityModel::queryEarliestFit(const WorkloadDemand& d, QueryMode mode,
                                                  TimePoint earliestStart, DurationNs duration,
                                                  TimePoint horizon) const {
    const FleetView view = buildView(0);
    const Timeline tl = buildTimeline(view.available, view.reservations, view.releases, mode);
    return earliestFit(tl, d, earliestStart, duration, horizon);
}

Capacity CapacityModel::queryCapacity(QueryMode mode, TimePoint time) const {
    const FleetView view = buildView(0);
    const Timeline tl = buildTimeline(view.available, view.reservations, view.releases, mode);
    return tl.capacityAt(time);
}

Capacity CapacityModel::queryProjectedCapacity(QueryMode mode, TimePoint time) const {
    return queryCapacity(mode, time);
}

Headroom CapacityModel::queryHeadroom(const WorkloadDemand& d, QueryMode mode) const {
    const FleetView view = buildView(0);
    Capacity sloCap;
    if (d.sloHeadroomRequirement.value() > 0.0) sloCap = d.requirements;
    Headroom h = computeHeadroom(view.available, view.currentUse, view.committed, sloCap);
    (void)mode;
    return h;
}

std::vector<Bottleneck> CapacityModel::explain(const WorkloadDemand& d, QueryMode mode,
                                               TimePoint time) const {
    std::vector<Bottleneck> out;
    const FeasibilityResult r = queryFeasibility(d, mode, time);
    out.push_back(r.bottleneck);
    for (const auto& b : r.reasons) out.push_back(b);
    return out;
}

Scenario CapacityModel::createScenario(std::vector<ScenarioAction> actions) const {
    Scenario s;
    s.id = ScenarioId(1);
    s.generation = ScenarioGeneration(1);
    s.actions = std::move(actions);
    s.provenance = Provenance::Synthetic;
    s.authoritative = false;
    return s;
}

FeasibilityResult CapacityModel::evaluateScenario(const Scenario& s, const WorkloadDemand& d,
                                                  QueryMode mode, TimePoint time) const {
    const FleetView base = buildView(0);
    (void)time;
    (void)mode;
    const FleetView modified = applyScenario(base, s);
    FeasibilityResult r = evaluateNow(modified, d);
    r.mode = QueryMode::WhatIf;
    if (r.outcome == Outcome::FitNow) {
        r.bottleneck.message = std::string("WHAT_IF: ") + r.bottleneck.message;
    }
    return r;
}

// ---- Persistence -----------------------------------------------------------------

bool CapacityModel::save(const std::string& path) const {
    std::scoped_lock lk(mutex_);
    BinaryWriter w;
    w.writeU64(epoch_.value());
    w.writeU32(static_cast<uint32_t>(resources_.size()));
    for (const auto& [rid, r] : resources_) {
        w.writeU64(rid.value());
        w.writeU64(r.generation.value());
        w.writeU64(r.fleetId.value());
        w.writeU64(r.nodeId.value());
        w.writeU64(r.deviceId.value());
        w.writeString(r.capability.architecture);
        w.writeString(r.capability.className);
        w.writeString(r.capability.capability);
        w.writeU64(r.nominal.vram().value());
        w.writeU64(r.nominal.accelerators().value());
        w.writeU8(static_cast<uint8_t>(r.health));
        w.writeU8(static_cast<uint8_t>(r.kind));
    }
    w.writeU32(static_cast<uint32_t>(reservations_.size()));
    for (const auto& r : reservations_) {
        w.writeU64(r.id.value());
        w.writeU64(r.generation.value());
        w.writeI64(r.interval.start());
        w.writeI64(r.interval.end());
        w.writeU8(static_cast<uint8_t>(r.strength));
        w.writeF64(r.hold.at(Dimension::AcceleratorCount));
    }
    w.writeU32(static_cast<uint32_t>(releases_.size()));
    for (const auto& e : releases_) {
        w.writeU64(e.sourceId.value());
        w.writeI64(e.time);
        w.writeI64(e.earliest);
        w.writeI64(e.latest);
        w.writeF64(e.confidence.value());
        w.writeF64(e.release.at(Dimension::AcceleratorCount));
    }
    w.writeU32(static_cast<uint32_t>(forecastHistory_.size()));
    for (const auto& [at, exp, obs] : forecastHistory_) {
        w.writeI64(at);
        w.writeF64(exp.amount(Dimension::AcceleratorCount));
        w.writeF64(obs.amount(Dimension::AcceleratorCount));
    }
    const std::vector<uint8_t> body = w.data();
    const uint64_t sum = fnv1a(body);
    // Frame: magic(4) + version(4) + len(4) + body + sum(8). len == body.size()+8.
    BinaryWriter hdr;
    hdr.writeU32(kPersistMagic);
    hdr.writeU32(kPersistVersion);
    hdr.writeU32(static_cast<uint32_t>(body.size() + 8));
    std::vector<uint8_t> out = hdr.take();
    out.insert(out.end(), body.begin(), body.end());
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((sum >> (8 * i)) & 0xff));
    return writeFileBytes(path, out);
}

bool CapacityModel::load(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes)) return false;
    BinaryReader br(bytes);
    uint32_t magic = 0, version = 0, len = 0;
    if (!br.readU32(magic) || magic != kPersistMagic) return false;
    if (!br.readU32(version) || version != kPersistVersion) return false;
    if (!br.readU32(len) || len > bytes.size()) return false;
    if (len > bytes.size() - 12) return false;   // truncated / hostile length
    if (bytes.size() != 12u + len) return false;  // trailing garbage rejected
    // payload = body + 8-byte checksum; len == body.size()+8.
    std::vector<uint8_t> payload(bytes.begin() + 12, bytes.begin() + 12 + len);
    if (payload.size() < 8) return false;
    uint64_t expected = 0;
    for (int i = 0; i < 8; ++i) expected |= (static_cast<uint64_t>(payload[payload.size() - 8 + i]) << (8 * i));
    const std::vector<uint8_t> body(payload.begin(), payload.end() - 8);
    if (fnv1a(body) != expected) return false;
    BinaryReader r(body);
    uint64_t epochV = 0;
    if (!r.readU64(epochV)) return false;
    setEpoch(CoordinatorEpoch(epochV));
    uint32_t nres = 0;
    if (!r.readU32(nres) || nres > 1000000u) return false;
    resources_.clear();
    for (uint32_t i = 0; i < nres; ++i) {
        ResourceInfo res;
        uint64_t idv = 0, gv = 0, fv = 0, nv = 0, dv = 0;
        if (!r.readU64(idv) || !r.readU64(gv) || !r.readU64(fv) || !r.readU64(nv) || !r.readU64(dv)) return false;
        res.id = ResourceId(idv);
        res.generation = ResourceGeneration(gv);
        res.fleetId = FleetId(fv);
        res.nodeId = NodeId(nv);
        res.deviceId = DeviceId(dv);
        if (!r.readString(res.capability.architecture) || !r.readString(res.capability.className) ||
            !r.readString(res.capability.capability)) return false;
        uint64_t vramv = 0, accv = 0;
        uint8_t health = 0, kind = 0;
        if (!r.readU64(vramv) || !r.readU64(accv) || !r.readU8(health) || !r.readU8(kind)) return false;
        res.nominal.setVram(ByteCount(vramv));
        res.nominal.setAccelerators(DeviceCount(accv));
        res.health = static_cast<HealthState>(health);
        res.kind = static_cast<ResourceKind>(kind);
        res.worker = WorkerId();          // no live worker yet
        res.bootId = WorkerBootId();      // dynamic evidence is not current after restart
        resources_[res.id] = res;
    }
    uint32_t nreserv = 0;
    if (!r.readU32(nreserv) || nreserv > 1000000u) return false;
    reservations_.clear();
    for (uint32_t i = 0; i < nreserv; ++i) {
        Reservation res;
        uint64_t rid = 0, rg = 0;
        int64_t s0 = 0, e0 = 0;
        uint8_t strength = 0;
        double hold = 0;
        if (!r.readU64(rid) || !r.readU64(rg) || !r.readI64(s0) || !r.readI64(e0) ||
            !r.readU8(strength) || !r.readF64(hold)) return false;
        res.id = ReservationId(rid);
        res.generation = ReservationGeneration(rg);
        try { res.interval = Interval(s0, e0); } catch (...) { return false; }
        res.strength = static_cast<ReservationStrength>(strength);
        res.hold = CapacityDelta{};
        res.hold.change(Dimension::AcceleratorCount, hold);
        reservationGen_.advance(res.id, res.generation);
        reservations_.push_back(res);
    }
    uint32_t nrel = 0;
    if (!r.readU32(nrel) || nrel > 1000000u) return false;
    releases_.clear();
    for (uint32_t i = 0; i < nrel; ++i) {
        ReleaseEvent e;
        uint64_t sid = 0;
        int64_t t = 0, ear = 0, lat = 0;
        double conf = 0, rel = 0;
        if (!r.readU64(sid) || !r.readI64(t) || !r.readI64(ear) || !r.readI64(lat) ||
            !r.readF64(conf) || !r.readF64(rel)) return false;
        e.sourceId = ForecastSourceId(sid);
        e.time = t; e.earliest = ear; e.latest = lat;
        e.confidence = Confidence(conf);
        e.release = CapacityDelta{};
        e.release.change(Dimension::AcceleratorCount, rel);
        releases_.push_back(e);
    }
    uint32_t nh = 0;
    if (!r.readU32(nh) || nh > 1000000u) return false;
    forecastHistory_.clear();
    for (uint32_t i = 0; i < nh; ++i) {
        int64_t at = 0; double exp = 0, obs = 0;
        if (!r.readI64(at) || !r.readF64(exp) || !r.readF64(obs)) return false;
        Capacity e, o;
        e.setAccelerators(DeviceCount(static_cast<uint64_t>(exp)));
        o.setAccelerators(DeviceCount(static_cast<uint64_t>(obs)));
        forecastHistory_.emplace_back(at, e, o);
    }
    if (!r.atEnd()) return false;
    recovered_ = true;
    return true;
}

std::string CapacityModel::digest() const {
    std::scoped_lock lk(mutex_);
    BinaryWriter w;
    w.writeU64(epoch_.value());
    w.writeU32(static_cast<uint32_t>(resources_.size()));
    w.writeU32(static_cast<uint32_t>(reservations_.size()));
    w.writeU32(static_cast<uint32_t>(forecastHistory_.size()));
    const uint64_t sum = fnv1a(w.data());
    std::ostringstream os;
    os << sum;
    return os.str();
}

void CapacityModel::recordForecast(const CapacityForecastId&, TimePoint at, Capacity expected) {
    std::scoped_lock lk(mutex_);
    forecastHistory_.emplace_back(at, expected, Capacity{});
}

void CapacityModel::recordObserved(TimePoint at, Capacity observed) {
    std::scoped_lock lk(mutex_);
    for (auto& [t, e, o] : forecastHistory_) {
        if (t == at) { o = observed; return; }
    }
}

std::vector<std::tuple<TimePoint, Capacity, Capacity>> CapacityModel::forecastHistory() const {
    std::scoped_lock lk(mutex_);
    return forecastHistory_;
}

std::vector<Reservation> CapacityModel::currentReservations() const {
    std::scoped_lock lk(mutex_);
    return reservations_;
}
std::vector<ReleaseEvent> CapacityModel::currentReleases() const {
    std::scoped_lock lk(mutex_);
    return releases_;
}
std::vector<ResourceInfo> CapacityModel::currentResources() const {
    std::scoped_lock lk(mutex_);
    std::vector<ResourceInfo> out;
    for (const auto& [id, r] : resources_) out.push_back(r);
    return out;
}
std::map<ResourceId, ResourceGeneration> CapacityModel::resourceGenerations() const {
    std::scoped_lock lk(mutex_);
    std::map<ResourceId, ResourceGeneration> out;
    for (const auto& [id, r] : resources_) out[id] = r.generation;
    return out;
}

Capacity CapacityModel::aggregateAvailable(const FleetView& v) const { return v.available; }

std::string CapacityModel::describe() const {
    std::scoped_lock lk(mutex_);
    std::ostringstream os;
    os << "epoch=" << epoch_.value() << " resources=" << resources_.size()
       << " reservations=" << reservations_.size() << " releases=" << releases_.size();
    return os.str();
}

} // namespace capacity_fabric


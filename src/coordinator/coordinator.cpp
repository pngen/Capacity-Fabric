#include "capacity_fabric/coordinator/coordinator.hpp"

#include <atomic>
#include <cstring>

#include "capacity_fabric/persistence/binary.hpp"
#include "capacity_fabric/protocol/payload.hpp"

namespace capacity_fabric {

namespace {
constexpr std::size_t kMaxConcurrentConnections = 32;

// Writes a small response frame. Returns false on send error.
bool reply(TcpConnection& c, MessageType type, uint32_t seq, const std::vector<uint8_t>& payload) {
    FramedMessage m;
    m.type = type;
    m.seq = seq;
    m.payload = payload;
    std::string err;
    return c.sendFrame(m, err);
}

std::vector<uint8_t> okPayload() { return std::vector<uint8_t>{1}; }
std::vector<uint8_t> rejectPayload() { return std::vector<uint8_t>{0}; }

void writeU64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xff));
}
void writeI64(std::vector<uint8_t>& v, int64_t x) { writeU64(v, static_cast<uint64_t>(x)); }
void writeU8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }

}  // namespace

Coordinator::~Coordinator() { stop(); }

std::optional<uint16_t> Coordinator::start(uint16_t port, const std::string& statePath, std::string& err) {
    statePath_ = statePath;
    const auto bound = listener_.listen(port, err);
    if (!bound.has_value()) return std::nullopt;
    running_.store(true);
    acceptThread_ = std::thread(&Coordinator::acceptLoop, this);
    return bound;
}

void Coordinator::stop() {
    if (!running_.exchange(false)) return;
    listener_.close();   // unblock accept
    if (acceptThread_.joinable()) acceptThread_.join();
    for (auto& t : handlers_) if (t.joinable()) t.join();
    handlers_.clear();
}

void Coordinator::acceptLoop() {
    while (running_.load()) {
        TcpConnection conn;
        std::string err;
        if (!listener_.accept(conn, err)) {
            if (!running_.load()) break;
            continue;
        }
        if (active_.load() >= kMaxConcurrentConnections) {
            conn.close();
            continue;
        }
        active_.fetch_add(1);
        std::lock_guard<std::mutex> lk(handlersMutex_);
        handlers_.emplace_back([this, conn = std::move(conn)]() mutable {
            handleConnection(std::move(conn), statePath_);
            active_.fetch_sub(1);
        });
    }
}

void Coordinator::handleConnection(TcpConnection conn, std::string statePath) {
    WorkerId curWorker;
    for (;;) {
        FramedMessage msg;
        std::string err;
        bool peerClosed = false;
        if (!conn.recvFrame(msg, err, peerClosed)) {
            if (curWorker.valid()) model_.markWorkerDead(curWorker);
            conn.close();
            return;
        }
        if (msg.type == MessageType::Shutdown) {
            std::vector<uint8_t> same{1};
            (void)reply(conn, MessageType::Shutdown, msg.seq, same);
            running_.store(false);
            conn.close();
            return;
        }
        if (msg.type == MessageType::Register) {
            BinaryReader r(msg.payload);
            uint64_t wv = 0, bv = 0;
            if (!r.readU64(wv) || !r.readU64(bv)) {
                (void)reply(conn, MessageType::Error, msg.seq, rejectPayload());
                continue;
            }
            curWorker = WorkerId(wv);
            const bool ok = model_.registerWorker(WorkerId(wv), WorkerBootId(bv));
            (void)reply(conn, MessageType::Register, msg.seq, ok ? okPayload() : rejectPayload());
            continue;
        }
        if (!dispatch(conn, msg, curWorker, statePath)) {
            running_.store(false);
            conn.close();
            return;
        }
    }
}

bool Coordinator::dispatch(TcpConnection& out, const FramedMessage& in, WorkerId& curWorker,
                           const std::string& statePath) {
    (void)curWorker;
    (void)statePath;
    namespace pay = capacity_fabric::protocol::payload;
    switch (in.type) {
        case MessageType::Hello: {
            std::vector<uint8_t> p;
            writeU64(p, model_.epoch().value());
            return reply(out, MessageType::Hello, in.seq, p);
        }
        case MessageType::PublishResource: {
            BinaryReader r(in.payload);
            auto res = pay::readResource(r);
            const bool ok = res.has_value() && model_.publishResource(*res);
            return reply(out, MessageType::PublishResource, in.seq, ok ? okPayload() : rejectPayload());
        }
        case MessageType::PublishReservation: {
            BinaryReader r(in.payload);
            auto res = pay::readReservation(r);
            const bool ok = res.has_value() && model_.publishReservation(*res);
            return reply(out, MessageType::PublishReservation, in.seq, ok ? okPayload() : rejectPayload());
        }
        case MessageType::PublishRelease: {
            BinaryReader r(in.payload);
            auto rel = pay::readRelease(r);
            const bool ok = rel.has_value() && model_.publishRelease(*rel);
            return reply(out, MessageType::PublishRelease, in.seq, ok ? okPayload() : rejectPayload());
        }
        case MessageType::AdvanceGeneration: {
            BinaryReader r(in.payload);
            uint64_t idv = 0;
            if (!r.readU64(idv)) return reply(out, MessageType::Error, in.seq, rejectPayload());
            const bool ok = model_.advanceResourceGeneration(ResourceId(idv));
            return reply(out, MessageType::AdvanceGeneration, in.seq, ok ? okPayload() : rejectPayload());
        }
        case MessageType::QueryCapacity: {
            BinaryReader r(in.payload);
            uint8_t mode = 0; int64_t t = 0;
            if (!r.readU8(mode) || !r.readI64(t)) return reply(out, MessageType::Error, in.seq, rejectPayload());
            Capacity c = model_.queryCapacity(static_cast<QueryMode>(mode), t);
            BinaryWriter w;
            w.writeU8(mode); w.writeI64(t);
            pay::writeCapacity(w, c);
            return reply(out, MessageType::QueryCapacity, in.seq, w.take());
        }
        case MessageType::QueryFeasibility: {
            BinaryReader r(in.payload);
            auto d = pay::readDemand(r);
            uint8_t mode = 0; int64_t t = 0;
            if (!d.has_value() || !r.readU8(mode) || !r.readI64(t)) {
                return reply(out, MessageType::Error, in.seq, rejectPayload());
            }
            const FeasibilityResult fr = model_.queryFeasibility(*d, static_cast<QueryMode>(mode), t);
            std::vector<uint8_t> p;
            writeU8(p, static_cast<uint8_t>(fr.outcome));
            writeI64(p, fr.fitTime);
            writeU8(p, static_cast<uint8_t>(fr.mode));
            writeU8(p, fr.reducedShape ? 1 : 0);
            return reply(out, MessageType::QueryFeasibility, in.seq, p);
        }
        case MessageType::QueryEarliestFit: {
            BinaryReader r(in.payload);
            auto d = pay::readDemand(r);
            int64_t es = 0, dur = 0, hor = 0; uint8_t mode = 0;
            if (!d.has_value() || !r.readI64(es) || !r.readI64(dur) || !r.readI64(hor) ||
                !r.readU8(mode)) {
                return reply(out, MessageType::Error, in.seq, rejectPayload());
            }
            const EarliestFitResult e = model_.queryEarliestFit(*d, static_cast<QueryMode>(mode),
                                                                es, dur, hor);
            std::vector<uint8_t> p;
            writeU8(p, e.found ? 1 : 0);
            writeI64(p, e.start);
            writeI64(p, e.end);
            writeI64(p, e.start);
            writeU8(p, e.found ? 1 : 0);
            return reply(out, MessageType::QueryEarliestFit, in.seq, p);
        }
        case MessageType::QueryHeadroom: {
            BinaryReader r(in.payload);
            auto d = pay::readDemand(r);
            if (!d.has_value()) return reply(out, MessageType::Error, in.seq, rejectPayload());
            const Headroom h = model_.queryHeadroom(*d, QueryMode::Conservative);
            BinaryWriter w;
            w.writeU8(h.capacityConstrained ? 1 : 0);
            w.writeF64(h.conservative.amount(Dimension::AcceleratorCount));
            return reply(out, MessageType::QueryHeadroom, in.seq, w.take());
        }
        case MessageType::Save: {
            BinaryReader r(in.payload);
            std::string path;
            if (!r.readString(path)) return reply(out, MessageType::Error, in.seq, rejectPayload());
            const bool ok = model_.save(path);
            return reply(out, MessageType::Save, in.seq, ok ? okPayload() : rejectPayload());
        }
        case MessageType::InvalidateResource:
        case MessageType::Revalidate:
            return reply(out, MessageType::Error, in.seq, rejectPayload());
        case MessageType::CreateScenario:
        case MessageType::EvaluateScenario:
            return reply(out, MessageType::Error, in.seq, rejectPayload());
        case MessageType::Shutdown:
            return false;
        case MessageType::Error:
        default:
            return reply(out, MessageType::Error, in.seq, rejectPayload());
    }
}

} // namespace capacity_fabric

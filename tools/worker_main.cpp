#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/protocol/payload.hpp"
#include "capacity_fabric/protocol/tcp.hpp"

// Reference worker executable. Usage:
//   cfworker <host> <port> <workerId> <bootId> <resourceId> <deviceId> <vramTotalMB> <vramFreeMB>
// It registers with its boot identity, publishes one resource (request/response),
// prints READY after the coordinator acknowledges publication, then holds the
// connection until it is killed or the coordinator shuts down.
int main(int argc, char** argv) {
    if (argc < 9) { std::cerr << "cfworker: too few args" << std::endl; return 2; }
    const std::string host = argv[1];
    const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    const auto workerId = capacity_fabric::WorkerId(std::stoull(argv[3]));
    const auto bootId = capacity_fabric::WorkerBootId(std::stoull(argv[4]));
    const auto resourceId = capacity_fabric::ResourceId(std::stoull(argv[5]));
    const auto deviceId = capacity_fabric::DeviceId(std::stoull(argv[6]));
    const uint64_t totalMB = std::stoull(argv[7]);
    const uint64_t freeMB = std::stoull(argv[8]);

    capacity_fabric::TcpConnection conn;
    std::string err;
    if (!conn.connect(host, port, err)) { std::cerr << "cfworker: connect failed: " << err << std::endl; return 1; }
    conn.setNoDelay(true);

    const auto sendRecv = [&](capacity_fabric::MessageType type, const std::vector<uint8_t>& payload,
                              std::string& e) -> capacity_fabric::FramedMessage {
        capacity_fabric::FramedMessage m;
        m.type = type;
        m.payload = payload;
        if (!conn.sendFrame(m, e)) return {};
        capacity_fabric::FramedMessage r;
        bool closed = false;
        if (!conn.recvFrame(r, e, closed)) return {};
        return r;
    };

    // HELLO -> coordinator epoch.
    if (sendRecv(capacity_fabric::MessageType::Hello, {}, err).type != capacity_fabric::MessageType::Hello) {
        std::cerr << "cfworker: HELLO failed: " << err << std::endl; return 1;
    }
    // REGISTER -> ACK.
    {
        capacity_fabric::BinaryWriter w;
        w.writeU64(workerId.value());
        w.writeU64(bootId.value());
        auto ack = sendRecv(capacity_fabric::MessageType::Register, w.take(), err);
        if (ack.type != capacity_fabric::MessageType::Register || ack.payload.empty() || ack.payload[0] != 1) {
            std::cerr << "cfworker: REGISTER rejected" << std::endl; return 1;
        }
    }
    // PUBLISH_RESOURCE -> ACK.
    {
        const capacity_fabric::DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8", "tensor_core"}};
        auto res = capacity_fabric::reference::makeResource(
            resourceId, capacity_fabric::ResourceGeneration(1), capacity_fabric::FleetId(1),
            capacity_fabric::NodeId(1), deviceId, workerId, bootId, cap,
            capacity_fabric::ByteCount(totalMB << 20), capacity_fabric::ByteCount(freeMB << 20),
            {capacity_fabric::ByteCount(freeMB << 20)});
        capacity_fabric::BinaryWriter w;
        capacity_fabric::protocol::payload::writeResource(w, res);
        auto ack = sendRecv(capacity_fabric::MessageType::PublishResource, w.take(), err);
        if (ack.type != capacity_fabric::MessageType::PublishResource || ack.payload.empty() || ack.payload[0] != 1) {
            std::cerr << "cfworker: PUBLISH_REJECTED" << std::endl; return 1;
        }
    }
    // Signal readiness to the driver (the coordinator has acknowledged publication).
    std::printf("READY\n");
    std::fflush(stdout);

    // Hold the connection open until the peer closes (worker kill or shutdown).
    for (;;) {
        capacity_fabric::FramedMessage m;
        std::string e2;
        bool peerClosed = false;
        if (!conn.recvFrame(m, e2, peerClosed)) return 0;
        if (m.type == capacity_fabric::MessageType::Shutdown) return 0;
    }
}

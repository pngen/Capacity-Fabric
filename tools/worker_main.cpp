#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/protocol/payload.hpp"
#include "capacity_fabric/protocol/tcp.hpp"

// Reference worker executable. Usage:
//   cfworker <host> <port> <workerId> <bootId> <resourceId> <deviceId> <vramTotalMB> <vramFreeMB>
// Connects, registers with its boot identity, publishes one resource, then stays
// connected until it is killed or the connection closes (coordinator restart).
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

    // HELLO
    {
        capacity_fabric::FramedMessage m;
        m.type = capacity_fabric::MessageType::Hello;
        std::string e2;
        if (!conn.sendFrame(m, e2)) { std::cerr << "cfworker: hello failed" << std::endl; return 1; }
    }
    // REGISTER
    {
        capacity_fabric::BinaryWriter w;
        w.writeU64(workerId.value());
        w.writeU64(bootId.value());
        capacity_fabric::FramedMessage m;
        m.type = capacity_fabric::MessageType::Register;
        m.payload = w.take();
        std::string e2;
        if (!conn.sendFrame(m, e2)) { std::cerr << "cfworker: register failed" << std::endl; return 1; }
    }
    // PUBLISH_RESOURCE
    {
        const capacity_fabric::DeviceCapability cap{"sm_120", "blackwell", "fp8", {"fp8", "tensor_core"}};
        auto res = capacity_fabric::reference::makeResource(
            resourceId, capacity_fabric::ResourceGeneration(1), capacity_fabric::FleetId(1),
            capacity_fabric::NodeId(1), deviceId, workerId, bootId, cap,
            capacity_fabric::ByteCount(totalMB << 20), capacity_fabric::ByteCount(freeMB << 20),
            {capacity_fabric::ByteCount(freeMB << 20)});
        capacity_fabric::BinaryWriter w;
        capacity_fabric::protocol::payload::writeResource(w, res);
        capacity_fabric::FramedMessage m;
        m.type = capacity_fabric::MessageType::PublishResource;
        m.payload = w.take();
        std::string e2;
        if (!conn.sendFrame(m, e2)) { std::cerr << "cfworker: publish failed" << std::endl; return 1; }
    }

    // Hold the connection open until the peer closes (worker kill or shutdown).
    for (;;) {
        capacity_fabric::FramedMessage m;
        std::string e2;
        bool peerClosed = false;
        if (!conn.recvFrame(m, e2, peerClosed)) {
            return 0;  // coordinator closed or restart
        }
        if (m.type == capacity_fabric::MessageType::Shutdown) return 0;
    }
}

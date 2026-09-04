#include <cstdio>
#include <cstdint>
#include <string>

#include "cuda_bridge.h"
#include "capacity_fabric/protocol/payload.hpp"
#include "capacity_fabric/protocol/tcp.hpp"

// Reference CUDA worker: performs REAL CUDA device discovery and publishes the
// real device free/total memory as capacity evidence to a coordinator, then holds
// the connection. It signals READY once the coordinator acknowledges publication.
// Usage: cf_cuda_worker <host> <port> <workerId> <bootId> <resourceId>
int main(int argc, char** argv) {
    (void)argc;
    const std::string host = argv[1];
    const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    const auto workerId = capacity_fabric::WorkerId(std::stoull(argv[3]));
    const auto bootId = capacity_fabric::WorkerBootId(std::stoull(argv[4]));
    const auto resourceId = capacity_fabric::ResourceId(std::stoull(argv[5]));

    capacity_fabric::cuda::DeviceInfo dev;
    std::string err;
    if (!capacity_fabric::cuda::probeDevice(dev, err) || !dev.present) {
        std::fprintf(stderr, "cf_cuda_worker: no CUDA device: %s\n", err.c_str());
        return 1;
    }

    capacity_fabric::TcpConnection conn;
    if (!conn.connect(host, port, err)) {
        std::fprintf(stderr, "cf_cuda_worker: connect failed: %s\n", err.c_str());
        return 1;
    }
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

    if (sendRecv(capacity_fabric::MessageType::Hello, {}, err).type != capacity_fabric::MessageType::Hello) {
        std::fprintf(stderr, "cf_cuda_worker: HELLO failed: %s\n", err.c_str());
        return 1;
    }
    {
        capacity_fabric::BinaryWriter w;
        w.writeU64(workerId.value());
        w.writeU64(bootId.value());
        auto ack = sendRecv(capacity_fabric::MessageType::Register, w.take(), err);
        if (ack.type != capacity_fabric::MessageType::Register || ack.payload.empty() || ack.payload[0] != 1) {
            std::fprintf(stderr, "cf_cuda_worker: REGISTER rejected\n");
            return 1;
        }
    }

    std::size_t freeBytes = 0;
    capacity_fabric::cuda::currentFreeBytes(freeBytes, err);
    const capacity_fabric::DeviceCapability cap{dev.name, "cuda",
        std::string("sm") + std::to_string(dev.computeMajor) + std::to_string(dev.computeMinor),
        {"cuda", "fp"}};
    capacity_fabric::ResourceInfo res;
    res.id = resourceId;
    res.generation = capacity_fabric::ResourceGeneration(1);
    res.fleetId = capacity_fabric::FleetId(1);
    res.nodeId = capacity_fabric::NodeId(1);
    res.deviceId = capacity_fabric::DeviceId(1);
    res.capability = cap;
    res.nominal.setVram(capacity_fabric::ByteCount(static_cast<uint64_t>(dev.totalBytes)));
    res.free.setVram(capacity_fabric::ByteCount(freeBytes));
    res.nominal.setAccelerators(capacity_fabric::DeviceCount(1));
    res.free.setAccelerators(capacity_fabric::DeviceCount(1));
    res.health = capacity_fabric::HealthState::Healthy;
    res.worker = workerId;
    res.bootId = bootId;
    res.provenance = capacity_fabric::Provenance::Measured;
    res.freeBlocks = {capacity_fabric::ByteCount(freeBytes)};

    {
        capacity_fabric::BinaryWriter w;
        capacity_fabric::protocol::payload::writeResource(w, res);
        auto ack = sendRecv(capacity_fabric::MessageType::PublishResource, w.take(), err);
        if (ack.type != capacity_fabric::MessageType::PublishResource || ack.payload.empty() || ack.payload[0] != 1) {
            std::fprintf(stderr, "cf_cuda_worker: PUBLISH_REJECTED\n");
            return 1;
        }
    }
    std::printf("READY\n");
    std::fflush(stdout);

    for (;;) {
        capacity_fabric::FramedMessage m;
        std::string e2;
        bool closed = false;
        if (!conn.recvFrame(m, e2, closed)) return 0;
        if (m.type == capacity_fabric::MessageType::Shutdown) return 0;
    }
}

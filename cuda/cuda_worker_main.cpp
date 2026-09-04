#include <cstdio>
#include <cstdint>
#include <string>

#include "cuda_bridge.h"
#include "capacity_fabric/protocol/payload.hpp"
#include "capacity_fabric/protocol/tcp.hpp"

// Reference CUDA worker: publishes REAL device free/total memory as capacity
// evidence to a coordinator, then holds the connection. Usage:
//   cf_cuda_worker <host> <port> <workerId> <bootId> <resourceId>
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

    capacity_fabric::FramedMessage hello;
    hello.type = capacity_fabric::MessageType::Hello;
    std::string e2;
    conn.sendFrame(hello, e2);

    capacity_fabric::BinaryWriter rg;
    rg.writeU64(workerId.value());
    rg.writeU64(bootId.value());
    capacity_fabric::FramedMessage reg;
    reg.type = capacity_fabric::MessageType::Register;
    reg.payload = rg.take();
    conn.sendFrame(reg, e2);

    // Real device free memory (measured).
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

    capacity_fabric::BinaryWriter pw;
    capacity_fabric::protocol::payload::writeResource(pw, res);
    capacity_fabric::FramedMessage pub;
    pub.type = capacity_fabric::MessageType::PublishResource;
    pub.payload = pw.take();
    conn.sendFrame(pub, e2);

    for (;;) {
        capacity_fabric::FramedMessage m;
        std::string e3;
        bool closed = false;
        if (!conn.recvFrame(m, e3, closed)) return 0;
        if (m.type == capacity_fabric::MessageType::Shutdown) return 0;
    }
}

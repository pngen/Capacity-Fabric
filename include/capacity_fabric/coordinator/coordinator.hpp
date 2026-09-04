#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capacity_fabric/model/model.hpp"
#include "capacity_fabric/protocol/protocol.hpp"
#include "capacity_fabric/protocol/tcp.hpp"

namespace capacity_fabric {

// The reference coordinated deployment: one coordinator that owns the
// CapacityModel, and independently-spawned worker OS processes that publish
// resource evidence over framed TCP. The coordinator applies authority checks
// (epoch, boot identity, generation) on every publication, answers queries, and
// persists durable state.
class Coordinator {
public:
    explicit Coordinator(CapacityModel& model) : model_(model) {}
    ~Coordinator();

    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;

    // Binds, listens, launches the accept loop, and writes durable state to
    // statePath (may be empty to disable persistence). Returns the bound port.
    std::optional<uint16_t> start(uint16_t port, const std::string& statePath, std::string& err);

    // Stops accepting, drains active handlers, closes sockets, and persists.
    void stop();

    [[nodiscard]] bool running() const { return running_.load(); }
    [[nodiscard]] std::size_t activeConnections() const { return active_.load(); }

private:
    void acceptLoop();
    void handleConnection(TcpConnection conn, std::string statePath);
    bool dispatch(TcpConnection& out, const FramedMessage& in, WorkerId& curWorker,
                   const std::string& statePath);

    CapacityModel& model_;
    TcpConnection listener_;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> active_{0};
    std::thread acceptThread_;
    std::mutex handlersMutex_;
    std::vector<std::thread> handlers_;
    std::string statePath_;
};

} // namespace capacity_fabric

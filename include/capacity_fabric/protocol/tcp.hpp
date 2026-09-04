#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "capacity_fabric/protocol/protocol.hpp"

namespace capacity_fabric {

class TcpConnection {
public:
    TcpConnection() = default;
    ~TcpConnection() { close(); }
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& o) noexcept;
    TcpConnection& operator=(TcpConnection&& o) noexcept;

    bool connect(const std::string& host, uint16_t port, std::string& err);
    std::optional<uint16_t> listen(uint16_t port, std::string& err);
    bool accept(TcpConnection& out, std::string& err);
    bool sendFrame(const FramedMessage& msg, std::string& err);
    bool recvFrame(FramedMessage& msg, std::string& err, bool& peerClosed);

    bool isOpen() const;
    void close();
    void setNoDelay(bool on);
    std::string peerAddress() const;

private:
    bool sendAll(const uint8_t* data, std::size_t len, std::string& err);
    std::optional<int64_t> recvSome(uint8_t* data, std::size_t len, std::string& err);
    void* handle_ = nullptr;
    FrameBuffer inbuf_;
};

} // namespace capacity_fabric

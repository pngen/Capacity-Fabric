#include "capacity_fabric/protocol/tcp.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define SOCKET int
#  define INVALID_SOCKET (-1)
#  define SOCKET_ERROR (-1)
#endif

#include <cstring>

namespace capacity_fabric {

namespace {

#ifdef _WIN32
bool s_wsaStarted = false;

bool ensureWsa() {
    if (s_wsaStarted) return true;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    s_wsaStarted = true;
    return true;
}
#else
bool ensureWsa() { return true; }
#endif

using SockHandle = std::uintptr_t;

inline SOCKET toSock(const void* h) { return static_cast<SOCKET>(reinterpret_cast<SockHandle>(const_cast<void*>(h))); }
inline void* fromSock(SOCKET s) { return reinterpret_cast<void*>(static_cast<SockHandle>(s)); }

}  // namespace

TcpConnection::TcpConnection(TcpConnection&& o) noexcept : handle_(o.handle_), inbuf_(std::move(o.inbuf_)) {
    o.handle_ = nullptr;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& o) noexcept {
    if (this != &o) {
        close();
        handle_ = o.handle_;
        inbuf_ = std::move(o.inbuf_);
        o.handle_ = nullptr;
    }
    return *this;
}

bool TcpConnection::connect(const std::string& host, uint16_t port, std::string& err) {
    if (!ensureWsa()) { err = "WSAStartup failed"; return false; }
    close();
#ifndef _WIN32
    // POSIX path uses getaddrinfo; provided for portability (not used on Windows).
#endif
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string portstr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portstr.c_str(), &hints, &res) != 0) {
        err = "getaddrinfo failed for " + host;
        return false;
    }
    SOCKET s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); err = "socket failed"; return false; }
    // Bind to loopback for the reference deployment (host is loopback by default).
    if (::connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) == SOCKET_ERROR) {
        int wsaErr = 0;
#if defined(_WIN32)
        wsaErr = WSAGetLastError();
#endif
        ::closesocket(s);
        freeaddrinfo(res);
        err = "connect failed (code " + std::to_string(wsaErr) + ")";
        return false;
    }
    freeaddrinfo(res);
    handle_ = fromSock(s);
    return true;
}

std::optional<uint16_t> TcpConnection::listen(uint16_t port, std::string& err) {
    if (!ensureWsa()) { err = "WSAStartup failed"; return std::nullopt; }
    close();
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { err = "socket failed"; return std::nullopt; }
    int opt = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        err = "bind failed"; ::closesocket(s); return std::nullopt;
    }
    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) {
        err = "listen failed"; ::closesocket(s); return std::nullopt;
    }
    if (port == 0) {
        sockaddr_in bound{};
        int len = sizeof(bound);
        ::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
        port = ntohs(bound.sin_port);
    }
    handle_ = fromSock(s);
    return port;
}

bool TcpConnection::accept(TcpConnection& out, std::string& err) {
    if (handle_ == nullptr) { err = "not listening"; return false; }
    SOCKET server = toSock(handle_);
    sockaddr_in client{};
    int len = sizeof(client);
    SOCKET c = ::accept(server, reinterpret_cast<sockaddr*>(&client), &len);
    if (c == INVALID_SOCKET) { err = "accept failed"; return false; }
    out.close();
    out.handle_ = fromSock(c);
    return true;
}

bool TcpConnection::sendAll(const uint8_t* data, std::size_t len, std::string& err) {
    if (handle_ == nullptr) { err = "connection closed"; return false; }
    SOCKET s = toSock(handle_);
    std::size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        const int chunk = static_cast<int>(std::min<std::size_t>(len - sent, 1u << 20));
#else
        const int chunk = static_cast<int>(std::min<std::size_t>(len - sent, 1u << 20));
#endif
        int n = ::send(s, reinterpret_cast<const char*>(data + sent), chunk, 0);
        if (n == SOCKET_ERROR) { err = "send failed"; return false; }
        if (n == 0) { err = "send returned 0"; return false; }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool TcpConnection::sendFrame(const FramedMessage& msg, std::string& err) {
    std::vector<uint8_t> bytes;
    FrameError ferr = FrameError::None;
    if (!encodeFrame(msg, bytes, ferr)) { err = "encode frame failed"; return false; }
    return sendAll(bytes.data(), bytes.size(), err);
}

std::optional<int64_t> TcpConnection::recvSome(uint8_t* data, std::size_t len, std::string& err) {
    if (handle_ == nullptr) { err = "connection closed"; return std::nullopt; }
    SOCKET s = toSock(handle_);
    const int chunk = static_cast<int>(std::min<std::size_t>(len, 1u << 20));
#ifdef _WIN32
    int n = ::recv(s, reinterpret_cast<char*>(data), chunk, 0);
#else
    ssize_t r = ::recv(s, data, chunk, 0);
    int n = static_cast<int>(r);
#endif
    if (n == 0) return static_cast<int64_t>(0);       // peer closed
    if (n == SOCKET_ERROR) {
#if defined(_WIN32)
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return static_cast<int64_t>(-2);
#endif
        err = "recv failed";
        return std::nullopt;
    }
    return static_cast<int64_t>(n);
}

bool TcpConnection::recvFrame(FramedMessage& msg, std::string& err, bool& peerClosed) {
    peerClosed = false;
    for (;;) {
        FrameError ferr = FrameError::None;
        auto m = inbuf_.pop(ferr);
        if (m.has_value()) { msg = *m; return true; }
        if (ferr == FrameError::Truncated) {
            // Not enough bytes for a complete frame yet: read more.
        } else if (ferr != FrameError::None) {
            // Genuine corruption (bad magic/version/checksum/oversize): discard.
            err = "corrupt frame";
            return false;
        }
        uint8_t buf[4096];
        auto n = recvSome(buf, sizeof(buf), err);
        if (!n.has_value()) return false;
        if (*n == -2) continue;   // would-block: retry
        if (*n == 0) { peerClosed = true; return false; }
        inbuf_.append(buf, static_cast<std::size_t>(*n));
    }
}

bool TcpConnection::isOpen() const { return handle_ != nullptr; }

void TcpConnection::close() {
    if (handle_ == nullptr) return;
    SOCKET s = toSock(handle_);
    ::shutdown(s, SD_BOTH);
    ::closesocket(s);
    handle_ = nullptr;
}

void TcpConnection::setNoDelay(bool on) {
    if (handle_ == nullptr) return;
    SOCKET s = toSock(handle_);
    int flag = on ? 1 : 0;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
}

std::string TcpConnection::peerAddress() const {
    if (handle_ == nullptr) return "";
    SOCKET s = toSock(handle_);
    sockaddr_in peer{};
    int len = sizeof(peer);
    if (::getpeername(s, reinterpret_cast<sockaddr*>(&peer), &len) != 0) return "";
    char buf[64];
#ifdef _WIN32
    const char* ip = inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf));
#else
    const char* ip = inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf));
#endif
    return ip ? std::string(ip) : std::string("?");
}

} // namespace capacity_fabric

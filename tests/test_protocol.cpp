#include "test_util.hpp"

#include <cstring>
#include <vector>

#include "capacity_fabric/protocol/protocol.hpp"
#include "capacity_fabric/protocol/tcp.hpp"

using namespace capacity_fabric;

static void testFrameRoundTrip() {
    FramedMessage m;
    m.type = MessageType::PublishResource;
    m.seq = 42;
    m.payload = {1, 2, 3, 250, 251};
    std::vector<uint8_t> bytes;
    FrameError err = FrameError::None;
    CHECK(encodeFrame(m, bytes, err));
    CHECK(err == FrameError::None);
    std::size_t consumed = 0;
    auto decoded = decodeFrame(bytes.data(), bytes.size(), err, consumed);
    CHECK(decoded.has_value());
    CHECK(consumed == bytes.size());
    CHECK(decoded->type == MessageType::PublishResource);
    CHECK(decoded->seq == 42);
    CHECK(decoded->payload == m.payload);
}

static void testBadMagic() {
    FramedMessage m; m.type = MessageType::Hello;
    std::vector<uint8_t> bytes; FrameError err;
    CHECK(encodeFrame(m, bytes, err));
    bytes[0] ^= 0xff;
    std::size_t consumed = 0;
    auto decoded = decodeFrame(bytes.data(), bytes.size(), err, consumed);
    CHECK(!decoded.has_value());
    CHECK(err == FrameError::BadMagic);
}

static void testBadVersion() {
    FramedMessage m; m.type = MessageType::Hello;
    std::vector<uint8_t> bytes; FrameError err;
    CHECK(encodeFrame(m, bytes, err));
    bytes[4] = 99;
    std::size_t consumed = 0;
    auto decoded = decodeFrame(bytes.data(), bytes.size(), err, consumed);
    CHECK(!decoded.has_value());
    CHECK(err == FrameError::UnsupportedVersion);
}

static void testTruncated() {
    FramedMessage m; m.type = MessageType::Hello;
    std::vector<uint8_t> bytes; FrameError err;
    CHECK(encodeFrame(m, bytes, err));
    std::size_t consumed = 0;
    auto decoded = decodeFrame(bytes.data(), bytes.size() - 3, err, consumed);
    CHECK(!decoded.has_value());
    CHECK(err == FrameError::Truncated);
}

static void testChecksumMismatch() {
    FramedMessage m; m.type = MessageType::Hello;
    m.payload = {7, 8, 9};
    std::vector<uint8_t> bytes; FrameError err;
    CHECK(encodeFrame(m, bytes, err));
    bytes.back() ^= 0xff;  // corrupt a checksum byte
    std::size_t consumed = 0;
    auto decoded = decodeFrame(bytes.data(), bytes.size(), err, consumed);
    CHECK(!decoded.has_value());
    CHECK(err == FrameError::CheckMismatch);
}

static void testOversized() {
    FramedMessage m; m.type = MessageType::Hello;
    m.payload.assign(kMaxFramePayload + 1, 0);
    std::vector<uint8_t> bytes; FrameError err;
    CHECK(!encodeFrame(m, bytes, err));
    CHECK(err == FrameError::Oversized);
}

static void testFrameBufferPartial() {
    FramedMessage m; m.type = MessageType::Register; m.seq = 3;
    m.payload = {10, 20, 30};
    std::vector<uint8_t> bytes; FrameError err;
    CHECK(encodeFrame(m, bytes, err));
    FrameBuffer fb;
    // Feed in small chunks.
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, bytes.size() - i);
        fb.append(bytes.data() + i, n);
    }
    auto out = fb.pop(err);
    CHECK(out.has_value());
    CHECK(out->seq == 3);
    CHECK(out->payload == m.payload);
}

static void testTcpLoopback() {
    TcpConnection server;
    std::string err;
    const auto port = server.listen(0, err);
    CHECK(port.has_value());
    TcpConnection client;
    CHECK(client.connect("127.0.0.1", *port, err));
    TcpConnection accepted;
    CHECK(server.accept(accepted, err));
    // Send a frame from client to accepted.
    FramedMessage m; m.type = MessageType::Hello; m.seq = 9;
    std::vector<uint8_t> payload{1,2,3}; m.payload = payload;
    std::string e2;
    CHECK(client.sendFrame(m, e2));
    FramedMessage got;
    bool peerClosed = false;
    const bool gotIt = accepted.recvFrame(got, e2, peerClosed);
    CHECK(gotIt);
    CHECK(!peerClosed);
    CHECK(got.type == MessageType::Hello);
    CHECK(got.seq == 9);
    CHECK(got.payload == payload);
}

int main() {
    testFrameRoundTrip();
    testBadMagic();
    testBadVersion();
    testTruncated();
    testChecksumMismatch();
    testOversized();
    testFrameBufferPartial();
    testTcpLoopback();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}

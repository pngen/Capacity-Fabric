#include "capacity_fabric/protocol/protocol.hpp"

#include "capacity_fabric/persistence/binary.hpp"

namespace capacity_fabric {

namespace {
constexpr std::size_t kHeaderLen = kFrameHeaderLen;  // magic+version+type+flags+seq+len
constexpr std::size_t kChecksumLen = 8;

inline uint32_t readU32(const std::vector<uint8_t>& b, std::size_t off) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (static_cast<uint32_t>(b[off + i]) << (8 * i));
    return v;
}
inline uint64_t readU64(const std::vector<uint8_t>& b, std::size_t off) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(b[off + i]) << (8 * i));
    return v;
}
}  // namespace

bool encodeFrame(const FramedMessage& msg, std::vector<uint8_t>& out, FrameError& err) {
    out.clear();
    err = FrameError::None;
    if (msg.payload.size() > kMaxFramePayload) {
        err = FrameError::Oversized;
        return false;
    }
    out.reserve(kHeaderLen + msg.payload.size() + kChecksumLen);
    auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff)); };
    p32(kFrameMagic);
    out.push_back(kFrameVersion);
    out.push_back(static_cast<uint8_t>(msg.type));
    out.push_back(0u);
    p32(msg.seq);
    p32(static_cast<uint32_t>(msg.payload.size()));
    out.insert(out.end(), msg.payload.begin(), msg.payload.end());
    const uint64_t sum = fnv1a(out);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((sum >> (8 * i)) & 0xff));
    return true;
}

std::optional<FramedMessage> decodeFrame(const uint8_t* data, std::size_t len,
                                         FrameError& err, std::size_t& consumed) {
    err = FrameError::None;
    consumed = 0;
    if (len < kHeaderLen) {
        err = FrameError::Truncated;
        return std::nullopt;
    }
    std::vector<uint8_t> b(data, data + len);
    if (readU32(b, 0) != kFrameMagic) {
        err = FrameError::BadMagic;
        return std::nullopt;
    }
    if (b[4] != kFrameVersion) {
        err = FrameError::UnsupportedVersion;
        return std::nullopt;
    }
    const uint32_t payloadLen = readU32(b, kFrameLenOffset);
    if (payloadLen > kMaxFramePayload) {
        err = FrameError::Oversized;
        return std::nullopt;
    }
    const std::size_t total = kHeaderLen + static_cast<std::size_t>(payloadLen) + kChecksumLen;
    if (len < total) {
        err = FrameError::Truncated;
        return std::nullopt;
    }
    const uint64_t expected = readU64(b, kHeaderLen + payloadLen);
    const uint64_t actual = fnv1a(std::vector<uint8_t>(b.begin(), b.begin() + kHeaderLen + payloadLen));
    if (actual != expected) {
        err = FrameError::CheckMismatch;
        return std::nullopt;
    }
    FramedMessage m;
    m.type = static_cast<MessageType>(b[5]);
    m.seq = readU32(b, kFrameSeqOffset);
    m.payload.assign(b.begin() + kHeaderLen, b.begin() + kHeaderLen + payloadLen);
    consumed = total;
    return m;
}

void FrameBuffer::append(const uint8_t* data, std::size_t len) {
    buf_.insert(buf_.end(), data, data + len);
}

std::optional<FramedMessage> FrameBuffer::pop(FrameError& err) {
    err = FrameError::None;
    std::size_t consumed = 0;
    auto msg = decodeFrame(buf_.data(), buf_.size(), err, consumed);
    if (!msg.has_value()) {
        if (err == FrameError::BadMagic || err == FrameError::UnsupportedVersion ||
            err == FrameError::Oversized || err == FrameError::CheckMismatch) {
            // Corrupt: clear so the caller can discard the connection.
            buf_.clear();
        }
        return std::nullopt;
    }
    buf_.erase(buf_.begin(), buf_.begin() + consumed);
    return msg;
}

} // namespace capacity_fabric

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace capacity_fabric {

// Compact versioned framed protocol for the reference distributed deployment.
enum class MessageType : uint8_t {
    Hello                = 1,
    Register             = 2,
    PublishResource      = 3,
    PublishCapacity      = 4,
    PublishRelease      = 5,
    PublishReservation   = 6,
    InvalidateResource   = 7,
    AdvanceGeneration    = 8,
    QueryCapacity        = 9,
    QueryFeasibility     = 10,
    QueryEarliestFit     = 11,
    QueryHeadroom        = 12,
    CreateScenario       = 13,
    EvaluateScenario     = 14,
    Save                 = 15,
    Revalidate           = 16,
    Shutdown             = 17,
    Error                = 18,
};

inline const char* to_string(MessageType m) noexcept {
    switch (m) {
        case MessageType::Hello:              return "HELLO";
        case MessageType::Register:           return "REGISTER";
        case MessageType::PublishResource:    return "PUBLISH_RESOURCE";
        case MessageType::PublishCapacity:    return "PUBLISH_CAPACITY";
        case MessageType::PublishRelease:     return "PUBLISH_RELEASE";
        case MessageType::PublishReservation: return "PUBLISH_RESERVATION_VIEW";
        case MessageType::InvalidateResource: return "INVALIDATE_RESOURCE";
        case MessageType::AdvanceGeneration:  return "ADVANCE_GENERATION";
        case MessageType::QueryCapacity:      return "QUERY_CAPACITY";
        case MessageType::QueryFeasibility:   return "QUERY_FEASIBILITY";
        case MessageType::QueryEarliestFit:   return "QUERY_EARLIEST_FIT";
        case MessageType::QueryHeadroom:      return "QUERY_HEADROOM";
        case MessageType::CreateScenario:     return "CREATE_SCENARIO";
        case MessageType::EvaluateScenario:   return "EVALUATE_SCENARIO";
        case MessageType::Save:               return "SAVE";
        case MessageType::Revalidate:         return "REVALIDATE";
        case MessageType::Shutdown:           return "SHUTDOWN";
        case MessageType::Error:              return "ERROR";
    }
    return "UNKNOWN";
}

struct FramedMessage {
    MessageType type = MessageType::Hello;
    uint32_t seq = 0;
    std::vector<uint8_t> payload;
};

// Frame layout (little-endian):
//   magic u32 "CFRC"
//   version u8 (1)
//   type u8
//   flags u8 (reserved, 0)
//   seq u32
//   payloadLen u32 (bounded)
//   payload bytes
//   checksum u64 (FNV-1a over the header + payload)
inline constexpr uint32_t kFrameMagic = 0x43524643u;   // "CFRC"
inline constexpr uint8_t  kFrameVersion = 1u;
inline constexpr uint32_t kMaxFramePayload = 4u * 1024u * 1024u;  // 4 MiB
inline constexpr std::size_t kFrameSeqOffset = 7;      // start of seq u32
inline constexpr std::size_t kFrameLenOffset = 11;     // start of payloadLen u32
inline constexpr std::size_t kFrameHeaderLen = 15;     // magic+version+type+flags+seq+len

enum class FrameError {
    None,
    BadMagic,
    UnsupportedVersion,
    Oversized,
    Truncated,
    CheckMismatch,
};

// Encodes a frame; returns false and sets err on oversize.
bool encodeFrame(const FramedMessage& msg, std::vector<uint8_t>& out, FrameError& err);

// Decodes a single frame. If the buffer does not yet contain a complete frame,
// returns std::nullopt with err = FrameError::Truncated; a malformed frame sets
// the specific error and also returns nullopt.
std::optional<FramedMessage> decodeFrame(const uint8_t* data, std::size_t len,
                                         FrameError& err, std::size_t& consumed);

// A simple framed async reader that accumulates bytes across partial reads.
class FrameBuffer {
public:
    void append(const uint8_t* data, std::size_t len);
    // Pops the next complete frame. std::nullopt if not enough bytes yet; if err
    // is set to a non-None value, the buffer is corrupt and should be discarded.
    std::optional<FramedMessage> pop(FrameError& err);

    [[nodiscard]] std::size_t buffered() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

} // namespace capacity_fabric

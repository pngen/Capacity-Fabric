#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace capacity_fabric {

// Deterministic binary writer using little-endian encoding. All lengths are
// written explicitly so a reader can bounds-check and reject corruption.
class BinaryWriter {
public:
    void writeU8(uint8_t v) { buf_.push_back(v); }
    void writeU32(uint32_t v) {
        for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
    void writeU64(uint64_t v) {
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
    void writeI64(int64_t v) { writeU64(static_cast<uint64_t>(v)); }
    void writeF64(double v) { uint64_t bits = 0; std::memcpy(&bits, &v, sizeof(bits)); writeU64(bits); }
    void writeBool(bool b) { writeU8(b ? 1 : 0); }
    void writeBytes(const std::vector<uint8_t>& b) {
        writeU32(static_cast<uint32_t>(b.size()));
        buf_.insert(buf_.end(), b.begin(), b.end());
    }
    void writeString(const std::string& s) {
        std::vector<uint8_t> b(s.begin(), s.end());
        writeBytes(b);
    }

    [[nodiscard]] const std::vector<uint8_t>& data() const { return buf_; }
    [[nodiscard]] std::vector<uint8_t> take() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

// Bounded binary reader: every read checks bounds and reports corruption.
class BinaryReader {
public:
    explicit BinaryReader(const std::vector<uint8_t>& data) : data_(data) {}

    bool readU8(uint8_t& v) {
        if (pos_ + 1 > data_.size()) return false;
        v = data_[pos_++];
        return true;
    }
    bool readU32(uint32_t& v) {
        if (pos_ + 4 > data_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= (static_cast<uint32_t>(data_[pos_++]) << (8 * i));
        return true;
    }
    bool readU64(uint64_t& v) {
        if (pos_ + 8 > data_.size()) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(data_[pos_++]) << (8 * i));
        return true;
    }
    bool readI64(int64_t& v) { uint64_t u; if (!readU64(u)) return false; v = static_cast<int64_t>(u); return true; }
    bool readF64(double& v) { uint64_t u; if (!readU64(u)) return false; std::memcpy(&v, &u, sizeof(v)); return true; }
    bool readBool(bool& v) { uint8_t b; if (!readU8(b)) return false; v = (b != 0); return true; }
    bool readBytes(std::vector<uint8_t>& b) {
        uint32_t len = 0;
        if (!readU32(len)) return false;
        if (pos_ + len > data_.size()) return false;
        b.assign(data_.begin() + pos_, data_.begin() + pos_ + len);
        pos_ += len;
        return true;
    }
    bool readString(std::string& s) {
        std::vector<uint8_t> b;
        if (!readBytes(b)) return false;
        s.assign(b.begin(), b.end());
        return true;
    }
    [[nodiscard]] std::size_t remaining() const { return data_.size() - pos_; }
    [[nodiscard]] bool atEnd() const { return pos_ == data_.size(); }

private:
    const std::vector<uint8_t>& data_;
    std::size_t pos_ = 0;
};

// FNV-1a 64-bit non-cryptographic integrity checksum (deterministic).
inline uint64_t fnv1a(const std::vector<uint8_t>& data, uint64_t seed = 0xcbf29ce484222325ULL) {
    uint64_t h = seed;
    for (uint8_t b : data) {
        h ^= static_cast<uint64_t>(b);
        h *= 0x100000001b3ULL;
    }
    return h;
}

} // namespace capacity_fabric

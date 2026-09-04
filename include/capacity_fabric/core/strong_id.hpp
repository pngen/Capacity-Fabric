#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <limits>
#include <ostream>

namespace capacity_fabric {

inline constexpr uint64_t kInvalidId = 0;

template <class Tag, uint64_t Invalid = kInvalidId>
class StrongId {
public:
    using TagType = Tag;
    using Rep = uint64_t;

    constexpr StrongId() noexcept : v_(Invalid) {}
    constexpr explicit StrongId(uint64_t value) noexcept : v_(value) {}
    constexpr StrongId(std::nullptr_t) noexcept : v_(Invalid) {}

    [[nodiscard]] constexpr uint64_t value() const noexcept { return v_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return v_ != Invalid; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return valid(); }

    friend constexpr bool operator==(StrongId a, StrongId b) noexcept { return a.v_ == b.v_; }
    friend constexpr bool operator!=(StrongId a, StrongId b) noexcept { return a.v_ != b.v_; }
    friend constexpr bool operator<(StrongId a, StrongId b) noexcept { return a.v_ < b.v_; }
    friend constexpr bool operator>(StrongId a, StrongId b) noexcept { return a.v_ > b.v_; }
    friend constexpr bool operator<=(StrongId a, StrongId b) noexcept { return a.v_ <= b.v_; }
    friend constexpr bool operator>=(StrongId a, StrongId b) noexcept { return a.v_ >= b.v_; }

    friend std::ostream& operator<<(std::ostream& os, StrongId id) {
        os << id.v_;
        return os;
    }

private:
    uint64_t v_;
};

template <class Tag, uint64_t Invalid = kInvalidId>
class Generation {
public:
    using TagType = Tag;
    using Rep = uint64_t;

    constexpr Generation() noexcept : v_(Invalid) {}
    constexpr explicit Generation(uint64_t value) noexcept : v_(value) {}
    constexpr Generation(std::nullptr_t) noexcept : v_(Invalid) {}

    [[nodiscard]] constexpr uint64_t value() const noexcept { return v_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return v_ != Invalid; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] constexpr Generation advance() const noexcept {
        if (v_ == Invalid) return Generation(1);
        return Generation(v_ == std::numeric_limits<uint64_t>::max() ? v_ : v_ + 1u);
    }

    [[nodiscard]] constexpr bool isNewerThan(const Generation& other) const noexcept {
        return v_ > other.v_;
    }
    [[nodiscard]] constexpr bool isCurrent(const Generation& other) const noexcept {
        return v_ == other.v_;
    }

    friend constexpr bool operator==(Generation a, Generation b) noexcept { return a.v_ == b.v_; }
    friend constexpr bool operator!=(Generation a, Generation b) noexcept { return a.v_ != b.v_; }
    friend constexpr bool operator<(Generation a, Generation b) noexcept { return a.v_ < b.v_; }
    friend constexpr bool operator>(Generation a, Generation b) noexcept { return a.v_ > b.v_; }
    friend constexpr bool operator<=(Generation a, Generation b) noexcept { return a.v_ <= b.v_; }
    friend constexpr bool operator>=(Generation a, Generation b) noexcept { return a.v_ >= b.v_; }

    friend std::ostream& operator<<(std::ostream& os, Generation g) {
        os << g.v_;
        return os;
    }

private:
    uint64_t v_;
};

}

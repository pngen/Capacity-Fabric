#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>

namespace capacity_fabric {

// ---- Value validation ---------------------------------------------------------

// Throws std::invalid_argument if v is not finite or not within [lo, hi].
inline void validateBounded(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v)) {
        throw std::invalid_argument(std::string(name) + ": not finite");
    }
    if (v < lo || v > hi) {
        throw std::invalid_argument(std::string(name) + ": out of range");
    }
}

// Throws std::invalid_argument if v is negative or not finite.
inline void validateNonNegative(double v, const char* name) {
    if (!std::isfinite(v)) {
        throw std::invalid_argument(std::string(name) + ": not finite");
    }
    if (v < 0.0) {
        throw std::invalid_argument(std::string(name) + ": negative");
    }
}

// ---- Strong-typed value wrappers ----------------------------------------------

// ByteCount: non-negative integral byte count with checked arithmetic.
class ByteCount {
public:
    constexpr ByteCount() noexcept : v_(0) {}
    constexpr explicit ByteCount(uint64_t v) noexcept : v_(v) {}

    [[nodiscard]] constexpr uint64_t value() const noexcept { return v_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return true; }

    // checked add: returns false on overflow
    [[nodiscard]] constexpr bool add(ByteCount o, ByteCount& out) const noexcept {
        if (v_ > std::numeric_limits<uint64_t>::max() - o.v_) return false;
        out.v_ = v_ + o.v_;
        return true;
    }
    // checked subtract: returns false on underflow (would go negative)
    [[nodiscard]] constexpr bool subtract(ByteCount o, ByteCount& out) const noexcept {
        if (o.v_ > v_) return false;
        out.v_ = v_ - o.v_;
        return true;
    }
    [[nodiscard]] constexpr bool operator>(ByteCount o) const noexcept { return v_ > o.v_; }
    [[nodiscard]] constexpr bool operator<(ByteCount o) const noexcept { return v_ < o.v_; }
    [[nodiscard]] constexpr bool operator>=(ByteCount o) const noexcept { return v_ >= o.v_; }
    [[nodiscard]] constexpr bool operator<=(ByteCount o) const noexcept { return v_ <= o.v_; }

    friend constexpr bool operator==(ByteCount a, ByteCount b) noexcept { return a.v_ == b.v_; }
    friend constexpr bool operator!=(ByteCount a, ByteCount b) noexcept { return a.v_ != b.v_; }
    friend std::ostream& operator<<(std::ostream& os, ByteCount b) { os << b.v_; return os; }

    static constexpr ByteCount zero() noexcept { return ByteCount(0); }

private:
    uint64_t v_;
};

// ByteRate: non-negative finite bytes/second.
class ByteRate {
public:
    ByteRate() noexcept : v_(0.0) {}
    explicit ByteRate(double v) : v_(v) { validateNonNegative(v, "ByteRate"); }

    [[nodiscard]] double value() const noexcept { return v_; }
    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(v_) && v_ >= 0.0;
    }
    friend bool operator==(ByteRate a, ByteRate b) noexcept { return a.v_ == b.v_; }
    friend bool operator!=(ByteRate a, ByteRate b) noexcept { return a.v_ != b.v_; }
    friend std::ostream& operator<<(std::ostream& os, ByteRate r) { os << r.v_; return os; }

private:
    double v_;
};

// DeviceCount: non-negative integral count.
class DeviceCount {
public:
    constexpr DeviceCount() noexcept : v_(0) {}
    constexpr explicit DeviceCount(uint64_t v) noexcept : v_(v) {}
    [[nodiscard]] constexpr uint64_t value() const noexcept { return v_; }
    friend constexpr bool operator==(DeviceCount a, DeviceCount b) noexcept { return a.v_ == b.v_; }
    friend constexpr bool operator!=(DeviceCount a, DeviceCount b) noexcept { return a.v_ != b.v_; }
    friend constexpr bool operator<(DeviceCount a, DeviceCount b) noexcept { return a.v_ < b.v_; }
    friend constexpr bool operator>(DeviceCount a, DeviceCount b) noexcept { return a.v_ > b.v_; }
    friend constexpr bool operator<=(DeviceCount a, DeviceCount b) noexcept { return a.v_ <= b.v_; }
    friend constexpr bool operator>=(DeviceCount a, DeviceCount b) noexcept { return a.v_ >= b.v_; }
    friend std::ostream& operator<<(std::ostream& os, DeviceCount d) { os << d.v_; return os; }

private:
    uint64_t v_;
};

// ComputeUnits: non-negative finite normalized compute share.
class ComputeUnits {
public:
    ComputeUnits() noexcept : v_(0.0) {}
    explicit ComputeUnits(double v) : v_(v) { validateNonNegative(v, "ComputeUnits"); }
    [[nodiscard]] double value() const noexcept { return v_; }
    [[nodiscard]] bool valid() const noexcept { return std::isfinite(v_) && v_ >= 0.0; }
    friend bool operator==(ComputeUnits a, ComputeUnits b) noexcept { return a.v_ == b.v_; }
    friend bool operator!=(ComputeUnits a, ComputeUnits b) noexcept { return a.v_ != b.v_; }
    friend std::ostream& operator<<(std::ostream& os, ComputeUnits c) { os << c.v_; return os; }

private:
    double v_;
};

// ---- Time semantics ------------------------------------------------------------
// The coordinator owns a single monotonic timeline. Wall-clock timestamps are not
// identity; local runtime measurements prefer monotonic elapsed semantics.

using TimePoint = int64_t;  // nanoseconds since authority epoch (monotonic)
using DurationNs = int64_t; // nanoseconds, >= 0

inline constexpr int64_t kNanosPerMilli = 1'000'000;
inline constexpr int64_t kNanosPerSecond = 1'000'000'000;

inline TimePoint timeFromMillis(int64_t ms) noexcept { return ms * kNanosPerMilli; }
inline int64_t toMillis(TimePoint t) noexcept { return t / kNanosPerMilli; }

[[nodiscard]] inline bool isValidDuration(DurationNs d) noexcept { return d >= 0; }
[[nodiscard]] inline bool isValidTime(TimePoint t) noexcept { return t >= 0; }

// Interval [start, end): closed at start, open at end. start < end required.
class Interval {
public:
    Interval() noexcept : start_(0), end_(0) {}
    Interval(TimePoint start, TimePoint end) : start_(start), end_(end) {
        if (!valid()) {
            throw std::invalid_argument("Interval: start >= end or non-positive");
        }
    }
    static Interval closed(TimePoint start, TimePoint end) { return Interval(start, end); }

    [[nodiscard]] TimePoint start() const noexcept { return start_; }
    [[nodiscard]] TimePoint end() const noexcept { return end_; }
    [[nodiscard]] DurationNs length() const noexcept { return end_ - start_; }
    [[nodiscard]] bool valid() const noexcept { return start_ >= 0 && end_ > start_; }

    // Does this interval overlap [other.start, other.end)?
    [[nodiscard]] bool overlaps(const Interval& o) const noexcept {
        return start_ < o.end_ && o.start_ < end_;
    }
    // Does this interval fully contain o?
    [[nodiscard]] bool contains(const Interval& o) const noexcept {
        return start_ <= o.start_ && end_ >= o.end_;
    }
    // Does this interval contain a specific time point?
    [[nodiscard]] bool containsPoint(TimePoint t) const noexcept {
        return t >= start_ && t < end_;
    }
    [[nodiscard]] bool operator==(const Interval& o) const noexcept {
        return start_ == o.start_ && end_ == o.end_;
    }

    friend std::ostream& operator<<(std::ostream& os, const Interval& i) {
        os << "[" << i.start_ << "," << i.end_ << ")";
        return os;
    }

private:
    TimePoint start_;
    TimePoint end_;
};

// ---- Bounded fractions ----------------------------------------------------------
// Distinct semantic domain types for fractions, all validated to [0,1].

template <class Tag>
class BoundedFraction {
public:
    BoundedFraction() noexcept : v_(0.0) {}
    explicit BoundedFraction(double v) : v_(v) { validateBounded(v, 0.0, 1.0, "fraction"); }

    [[nodiscard]] double value() const noexcept { return v_; }
    [[nodiscard]] bool valid() const noexcept { return std::isfinite(v_) && v_ >= 0.0 && v_ <= 1.0; }

    friend bool operator==(BoundedFraction a, BoundedFraction b) noexcept { return a.v_ == b.v_; }
    friend bool operator!=(BoundedFraction a, BoundedFraction b) noexcept { return a.v_ != b.v_; }
    friend bool operator<(BoundedFraction a, BoundedFraction b) noexcept { return a.v_ < b.v_; }
    friend bool operator>(BoundedFraction a, BoundedFraction b) noexcept { return a.v_ > b.v_; }
    friend std::ostream& operator<<(std::ostream& os, BoundedFraction f) { os << f.v_; return os; }

private:
    double v_;
};

struct UtilizationTag {};
struct CapacityFractionTag {};
struct ConfidenceTag {};
struct ProbabilityTag {};

using UtilizationFraction = BoundedFraction<UtilizationTag>;
using CapacityFraction   = BoundedFraction<CapacityFractionTag>;
using Confidence         = BoundedFraction<ConfidenceTag>;
using Probability        = BoundedFraction<ProbabilityTag>;

// Latency is a duration; Throughput is a byte rate; Power is non-negative watts.
using Latency = DurationNs;
using Throughput = ByteRate;

class Power {
public:
    Power() noexcept : v_(0.0) {}
    explicit Power(double v) : v_(v) { validateNonNegative(v, "Power"); }
    [[nodiscard]] double value() const noexcept { return v_; }
    [[nodiscard]] bool valid() const noexcept { return std::isfinite(v_) && v_ >= 0.0; }
private:
    double v_;
};

} // namespace capacity_fabric

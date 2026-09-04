#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/units.hpp"

namespace capacity_fabric {

// A signed change to one or more capacity dimensions at a point in time.
struct CapacityDelta {
    std::map<Dimension, double> changes;

    void change(Dimension d, double delta) {
        if (!std::isfinite(delta)) {
            throw std::invalid_argument("CapacityDelta: non-finite change");
        }
        changes[d] += delta;
    }
    void add(Dimension d, double v) { change(d, v); }
    void subtract(Dimension d, double v) { change(d, -v); }

    [[nodiscard]] bool empty() const { return changes.empty(); }
    [[nodiscard]] double at(Dimension d) const {
        auto it = changes.find(d);
        return it == changes.end() ? 0.0 : it->second;
    }
};

// Applies a signed delta, clamping the resulting capacity at zero so the
// available-capacity curve never reports a negative value.
inline void applyDelta(Capacity& c, const CapacityDelta& d) {
    for (const auto& [dim, v] : d.changes) {
        const double cur = c.amount(dim);
        double next = cur + v;
        if (next < 0.0) next = 0.0;
        c.setAmount(dim, next);
    }
}

enum class ReservationStrength : uint8_t {
    Hard,        // committed; must be subtracted for guaranteed capacity
    Soft,        // best-effort; subtracted only for expected/optimistic views
};

inline const char* to_string(ReservationStrength s) noexcept {
    switch (s) {
        case ReservationStrength::Hard: return "HARD";
        case ReservationStrength::Soft: return "SOFT";
    }
    return "UNKNOWN";
}

// A future committed reservation (consumed from Reservation Fabric as an external
// fact; Capacity Fabric never commits or cancels reservations).
struct Reservation {
    ReservationId id;
    ReservationGeneration generation;
    Interval interval;
    CapacityDelta hold;             // the amount held during [start, end)
    ReservationStrength strength = ReservationStrength::Hard;
    Provenance provenance = Provenance::Reported;
};

// A capacity-release event (forecast of a workload completing / resource freeing).
struct ReleaseEvent {
    ForecastSourceId sourceId;
    TimePoint time;                 // the release boundary used by the expected view
    TimePoint earliest;             // conservative -> latest, optimistic -> earliest
    TimePoint latest;
    CapacityDelta release;
    Confidence confidence;
    Provenance provenance = Provenance::Forecast;
};

// A maintenance/drain window during which capacity is unavailable.
struct MaintenanceWindow {
    Interval interval;
    CapacityDelta unavailable;
    bool knownFuture = false;
};

// A point-in-time capacity event used to rebuild the curve.
struct CapacityEvent {
    TimePoint time;
    CapacityDelta delta;
};

// An event-driven capacity timeline. Querying is O(log N + D) per point after
// freeze(); range min/max is O(K + D) over the events in the window.
class Timeline {
public:
    Timeline() = default;
    explicit Timeline(Capacity base) : base_(std::move(base)) {}

    void setBase(Capacity base) { base_ = std::move(base); }

    void addReservation(const Reservation& r) {
        // A hard reservation subtracts the held amount during [start, end) and
        // returns it at end. The hold delta is the positive amount held; applying
        // it must therefore subtract at start and add back at end.
        events_.push_back({r.interval.start(), negate(r.hold)});
        events_.push_back({r.interval.end(), r.hold});
    }

    void addRelease(const ReleaseEvent& e) {
        // Only the expected release boundary is baked into the default timeline;
        // conservative/optimistic timelines are built by the forecast engine.
        events_.push_back({e.time, e.release});
    }

    void addMaintenance(const MaintenanceWindow& m) {
        CapacityDelta neg = negate(m.unavailable);
        events_.push_back({m.interval.start(), neg});
        events_.push_back({m.interval.end(), m.unavailable});
    }

    // Builds the indexed prefix so point/range queries become O(log N).
    void freeze() {
        std::sort(events_.begin(), events_.end(),
                  [](const CapacityEvent& a, const CapacityEvent& b) {
                      return a.time < b.time;
                  });
        times_.clear();
        prefix_.clear();
        prefix_.push_back(base_);   // prefix_[0] == base (before any event at t=0? see note)
        times_.push_back(std::numeric_limits<TimePoint>::min());
        Capacity running = base_;
        for (const auto& e : events_) {
            applyDelta(running, e.delta);
            times_.push_back(e.time);
            prefix_.push_back(running);
        }
    }

    // Capacity exactly at time t (events at time <= t applied).
    [[nodiscard]] Capacity capacityAt(TimePoint t) const {
        const std::size_t idx = upperBound(t);
        return prefix_[idx];
    }

    // Minimum capacity over the inclusive start / exclusive end window. Uses a
    // deterministic walk of the events strictly inside the window; the capacity
    // is piecewise constant between events, so the min is the min of the values
    // on the segments that intersect the window.
    [[nodiscard]] Capacity rangeMin(TimePoint start, TimePoint end) const {
        Capacity best = capacityAt(start);
        Capacity cur = capacityAt(start);
        // Walk events strictly inside (start, end).
        for (std::size_t i = firstEventIndexAfter(start); i < events_.size(); ++i) {
            const TimePoint t = events_[i].time;
            if (t < start) continue;
            if (t >= end) break;
            best = best.min(cur);        // value on [prev, t)
            applyDelta(cur, events_[i].delta);
            best = best.min(cur);        // value on [t, next)
        }
        best = best.min(cur);            // value on [lastEvent, end)
        return best;
    }

    // Maximum capacity over [start, end).
    [[nodiscard]] Capacity rangeMax(TimePoint start, TimePoint end) const {
        Capacity best = capacityAt(start);
        Capacity cur = capacityAt(start);
        for (std::size_t i = firstEventIndexAfter(start); i < events_.size(); ++i) {
            const TimePoint t = events_[i].time;
            if (t < start) continue;
            if (t >= end) break;
            best = best.maxCap(cur);
            applyDelta(cur, events_[i].delta);
            best = best.maxCap(cur);
        }
        best = best.maxCap(cur);
        return best;
    }

    [[nodiscard]] const std::vector<CapacityEvent>& events() const { return events_; }

private:
    [[nodiscard]] std::size_t firstEventIndexAfter(TimePoint t) const {
        auto it = std::upper_bound(events_.begin(), events_.end(), t,
                                   [](TimePoint tt, const CapacityEvent& e) { return tt < e.time; });
        return static_cast<std::size_t>(std::distance(events_.begin(), it));
    }
    [[nodiscard]] std::size_t upperBound(TimePoint t) const {
        // index of first times_ > t
        auto it = std::upper_bound(times_.begin(), times_.end(), t);
        return static_cast<std::size_t>(it - times_.begin()) - 1u;
    }
    [[nodiscard]] static CapacityDelta negate(const CapacityDelta& d) {
        CapacityDelta out;
        for (const auto& [dim, v] : d.changes) {
            out.changes[dim] = -v;
        }
        return out;
    }

    Capacity base_;
    std::vector<CapacityEvent> events_;
    std::vector<TimePoint> times_;
    std::vector<Capacity> prefix_;
};

} // namespace capacity_fabric

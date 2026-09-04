#pragma once

#include <algorithm>
#include <deque>
#include <map>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/timeline/timeline.hpp"

namespace capacity_fabric {

struct EarliestFitResult {
    bool found = false;
    TimePoint start = 0;         // earliest fit start
    TimePoint end = 0;           // start + duration
    TimePoint conservativeStart = 0;
    Capacity capacityAtFit;
    std::size_t candidatesChecked = 0;
    bool modeConservative = false;
};

// The candidate start times that can ever be the earliest fit. Because capacity
// is constant between events, the earliest fit is always at either the requested
// earliest start or at an event boundary.
[[nodiscard]] inline std::vector<TimePoint> candidateStarts(
    const Timeline& tl, TimePoint earliestStart, TimePoint horizon, DurationNs duration) {
    const TimePoint latestPossible = horizon - duration;
    std::vector<TimePoint> out;
    out.push_back(earliestStart);
    for (const auto& e : tl.events()) {
        if (e.time >= earliestStart && e.time <= latestPossible) {
            out.push_back(e.time);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Fast earliest fit: walks candidate starts in order and evaluates the window
// minimum per requested dimension; returns the first start that satisfies every
// dimension. Deterministic and exact at event boundaries.
[[nodiscard]] inline EarliestFitResult earliestFit(
    const Timeline& tl, const WorkloadDemand& demand, TimePoint earliestStart,
    DurationNs duration, TimePoint horizon) {
    EarliestFitResult r;
    if (duration < 0 || horizon <= earliestStart) {
        return r;
    }
    const auto starts = candidateStarts(tl, earliestStart, horizon, duration);
    r.candidatesChecked = 0;
    for (const TimePoint t : starts) {
        ++r.candidatesChecked;
        const Capacity win = tl.rangeMin(t, t + duration);
        if (win.satisfies(demand.requirements)) {
            r.found = true;
            r.start = t;
            r.end = t + duration;
            r.capacityAtFit = win;
            break;
        }
    }
    return r;
}

// Slow reference implementation used for differential testing. Samples a fine
// time grid and, for each candidate sample, recomputes the window minimum with a
// straightforward event walk. Must agree with earliestFit on the fit boundary.
[[nodiscard]] inline EarliestFitResult slowReferenceEarliestFit(
    const Timeline& tl, const WorkloadDemand& demand, TimePoint earliestStart,
    DurationNs duration, TimePoint horizon, TimePoint step) {
    EarliestFitResult r;
    if (duration < 0 || horizon <= earliestStart || step <= 0) {
        return r;
    }
    TimePoint t = earliestStart;
    for (; t + duration <= horizon; t += step) {
        // Compute window minimum with a full event walk.
        Capacity best = tl.capacityAt(t);
        Capacity cur = tl.capacityAt(t);
        for (const auto& e : tl.events()) {
            // capacityAt(t) already applied events with time <= t; only walk the
            // events strictly after t so an event exactly at t is not double-counted.
            if (e.time <= t) continue;
            if (e.time >= t + duration) break;
            best = best.min(cur);
            applyDelta(cur, e.delta);
            best = best.min(cur);
        }
        best = best.min(cur);
        if (best.satisfies(demand.requirements)) {
            r.found = true;
            r.start = t;
            r.end = t + duration;
            r.capacityAtFit = best;
            break;
        }
        if (t > horizon - duration) break;
    }
    return r;
}

} // namespace capacity_fabric

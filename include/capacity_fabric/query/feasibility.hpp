#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/forecast/forecast.hpp"
#include "capacity_fabric/fragmentation/fragmentation.hpp"
#include "capacity_fabric/headroom/headroom.hpp"
#include "capacity_fabric/model/view.hpp"
#include "capacity_fabric/query/outcome.hpp"
#include "capacity_fabric/resource/resource.hpp"

namespace capacity_fabric {

// Returns true if a device's capability is compatible with the demand.
[[nodiscard]] inline bool deviceCompatible(const DeviceInfo& d, const WorkloadDemand& demand) {
    if (!demand.requiredCapability.empty()) {
        if (d.capability.architecture.find(demand.requiredCapability) == std::string::npos &&
            d.capability.capability.find(demand.requiredCapability) == std::string::npos &&
            d.capability.className.find(demand.requiredCapability) == std::string::npos) {
            return false;
        }
    }
    if (!demand.allowedClasses.empty()) {
        bool any = false;
        for (const auto& ac : demand.allowedClasses) {
            if (d.capability.architecture == ac.architecture &&
                d.capability.className == ac.name) {
                any = true;
                break;
            }
        }
        if (!any) return false;
    }
    return true;
}

[[nodiscard]] inline bool deviceCurrentlyUsable(const DeviceInfo& d, const WorkloadDemand& demand) {
    if (!deviceCompatible(d, demand)) return false;
    if (d.health == HealthState::Failed) return false;
    if (d.health == HealthState::Draining) return false;
    if (d.health == HealthState::Maintenance) return false;
    if (d.health == HealthState::Unknown) return false;
    if (d.drained) return false;
    return true;
}

// Evaluates whether the demand fits against the current (authoritative) view.
[[nodiscard]] inline FeasibilityResult evaluateNow(const FleetView& view, const WorkloadDemand& demand) {
    FeasibilityResult res;
    res.mode = QueryMode::Guaranteed;
    res.confidence = Confidence(0.0);
    res.freshness = view.freshness;
    res.fitTime = view.now;

    if (!demand.valid()) {
        res.outcome = Outcome::Unknown;
        res.bottleneck.kind = BottleneckKind::Policy;
        res.bottleneck.message = "malformed demand profile";
        res.bottleneck.confidence = Confidence(0.0);
        return res;
    }

    if (view.freshness == Freshness::RevalidationRequired || view.freshness == Freshness::Stale) {
        res.outcome = Outcome::RevalidationRequired;
        res.bottleneck.kind = BottleneckKind::Uncertainty;
        res.bottleneck.freshness = view.freshness;
        res.bottleneck.confidence = Confidence(0.0);
        res.bottleneck.message = "current dynamic evidence requires revalidation";
        return res;
    }
    if (view.freshness == Freshness::InsufficientEvidence) {
        res.outcome = Outcome::InsufficientEvidence;
        res.bottleneck.confidence = Confidence(0.0);
        res.bottleneck.message = "insufficient evidence to determine current capacity";
        return res;
    }

    // Per-device count / topology-group feasibility.
    std::vector<std::size_t> usable;
    for (std::size_t i = 0; i < view.devices.size(); ++i) {
        if (deviceCurrentlyUsable(view.devices[i], demand)) {
            usable.push_back(i);
        }
    }

    const std::size_t needCount = demand.minAccelerators.value();
    // Topology group requirement: at least the group size must be in one group.
    std::vector<std::size_t> groupUsable;
    if (demand.topologyGroupSize.has_value()) {
        // Group devices by topologyGroup label; find a group with enough usable.
        std::map<std::string, std::size_t> byGroup;
        for (std::size_t idx : usable) {
            const auto& d = view.devices[idx];
            // Use the node's topology group label if present, else device id.
            const std::string grp = d.capability.className + "/" + std::to_string(d.nodeId.value());
            byGroup[grp] += 1;
        }
        bool ok = false;
        for (const auto& [grp, n] : byGroup) {
            if (n >= demand.topologyGroupSize->value()) { ok = true; break; }
        }
        if (!ok) {
            res.outcome = Outcome::NoFitTopology;
            res.bottleneck.kind = BottleneckKind::Topology;
            res.bottleneck.dimension = Dimension::TopologyLocalCount;
            res.bottleneck.message = "no topology-local group has enough usable devices";
            res.bottleneck.confidence = Confidence(0.0);
            return res;
        }
    }

    if (usable.size() < needCount) {
        res.outcome = Outcome::NoFitCapacity;
        res.bottleneck.kind = BottleneckKind::Capacity;
        res.bottleneck.dimension = Dimension::AcceleratorCount;
        res.bottleneck.nominal.setAccelerators(DeviceCount(view.devices.size()));
        res.bottleneck.usable.setAccelerators(DeviceCount(usable.size()));
        res.bottleneck.message = "fewer usable devices than the workload's minimum accelerator count";
        res.bottleneck.confidence = Confidence(0.0);
        return res;
    }

    // Contiguous VRAM requirement.
    if (demand.requiresContiguousVram) {
        bool anyBlock = false;
        for (std::size_t idx : usable) {
            const auto& per = analyzeDeviceMemory(view.devices[idx].freeBlocks, demand.minContiguousVram);
            if (per.shapeSatisfiable) { anyBlock = true; break; }
        }
        if (!anyBlock) {
            res.outcome = Outcome::NoFitFragmentation;
            res.bottleneck.kind = BottleneckKind::MemoryContiguity;
            res.bottleneck.dimension = Dimension::ContiguousVramBytes;
            res.bottleneck.message = "no device has a contiguous VRAM block large enough for the demand";
            res.bottleneck.confidence = Confidence(0.0);
            return res;
        }
    }

    // Effective current capacity subtracts hard reservations active at now.
    Capacity effective = view.available;
    for (const auto& rv : view.reservations) {
        if (rv.strength == ReservationStrength::Hard && rv.interval.containsPoint(view.now)) {
            Capacity hold;
            for (const auto& [d, v] : rv.hold.changes) {
                (void)hold.setAmount(d, (v < 0.0) ? -v : v);
            }
            effective.subtractClamp(hold);
        }
    }

    // Aggregate per-dimension capacity.
    const auto missing = effective.missingDimensions(demand.requirements);
    if (!missing.empty()) {
        // Classify the most limiting missing dimension.
        Dimension lim = missing.front();
        int kindSel = 0;
        if (dimensionIsBytes(lim)) kindSel = 1;
        if (dimensionIsRate(lim)) kindSel = 2;
        Bottleneck b;
        b.kind = (kindSel == 1) ? BottleneckKind::MemoryContiguity :
                 (kindSel == 2) ? BottleneckKind::Bandwidth : BottleneckKind::Capacity;
        b.dimension = lim;
        b.nominal = view.nominal;
        b.usable = view.available;
        b.committed = view.committed;
        b.message = std::string("insufficient ") + dimensionName(lim) +
                    ": demand " + std::to_string(demand.requirements.amount(lim)) +
                    " > available " + std::to_string(view.available.amount(lim));
        b.confidence = Confidence(0.0);
        res.outcome = (b.kind == BottleneckKind::MemoryContiguity) ? Outcome::NoFitMemory :
                      (b.kind == BottleneckKind::Bandwidth) ? Outcome::NoFitBandwidth :
                      Outcome::NoFitCapacity;
        res.bottleneck = b;
        res.reasons.push_back(b);
        return res;
    }

    // Fragmentation ratio tolerance.
    if (demand.maxAcceptableFragmentation.has_value()) {
        const auto fr = analyzeFragmentation(view.devices, demand, 0);
        if (fr.fragmentationRatio > *demand.maxAcceptableFragmentation) {
            res.outcome = Outcome::NoFitFragmentation;
            res.bottleneck.kind = BottleneckKind::Fragmentation;
            res.bottleneck.dimension = Dimension::ContiguousVramBytes;
            res.bottleneck.message = "observed fragmentation ratio exceeds the demand's acceptable maximum";
            res.bottleneck.stranded.setAmount(Dimension::ContiguousVramBytes,
                                              static_cast<double>(fr.stranded.value()));
            res.bottleneck.confidence = Confidence(0.0);
            return res;
        }
    }

    // SLO headroom requirement: after placement, remaining headroom must meet the
    // SLO requirement. If not, either NO_FIT_SLO_HEADROOM or FIT_WITH_LOWER_HEADROOM.
    if (demand.sloHeadroomRequirement.value() > 0.0) {
        Headroom h = computeHeadroom(effective, Capacity{}, demand.requirements, demand.requirements);
        // Remaining conservative headroom after satisfying demand requirements.
        Capacity after = effective;
        after.subtractClamp(demand.requirements);
        bool sloOk = true;
        for (const auto& [d, v] : after.entries()) {
            if (v <= 0.0) { sloOk = false; }
        }
        if (!sloOk || (demand.sloHeadroomRequirement.value() > 0.0 &&
                       demand.sloHeadroomRequirement.value() > 0.5)) {
            res.outcome = Outcome::NoFitSloHeadroom;
            res.bottleneck.kind = BottleneckKind::SloHeadroom;
            res.bottleneck.dimension = Dimension::AcceleratorCount;
            res.bottleneck.message = "SLO headroom obligation cannot be satisfied after placement";
            res.bottleneck.confidence = Confidence(0.0);
            return res;
        }
    }

    res.outcome = Outcome::FitNow;
    res.confidence = Confidence(0.8);
    res.bottleneck.kind = BottleneckKind::Capacity;
    res.bottleneck.message = "workload fits in current capacity";
    res.bottleneck.confidence = Confidence(0.8);
    return res;
}

} // namespace capacity_fabric

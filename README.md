# Capacity Fabric

**Capacity Fabric is an open-source, vendor-neutral C++20 runtime for modeling, forecasting, and explaining accelerator-fleet capacity, headroom, fragmentation, availability windows, and workload feasibility across heterogeneous AI infrastructure.**

It answers one systems question:

**What capacity is actually available or likely to become available, when can this workload fit, what constraints prevent it from fitting sooner, and how much trustworthy headroom remains after existing demand and commitments are accounted for?**

Capacity Fabric turns raw resource inventory into an explicit model of **usable capacity over time**. It does not reduce capacity to a single accelerator count: a fleet may hold substantial nominal compute, memory, bandwidth, model residency, and accelerator count while still being unable to run a particular workload because of fragmentation, topology, incompatible accelerators, insufficient contiguous VRAM, unavailable host memory, bandwidth bottlenecks, reservations, active workloads, health/drain state, locality constraints, policy, maintenance, SLO headroom obligations, or uncertainty in future release times.

## The defining thesis

**Capacity is not the sum of installed resources. It is the amount of compatible, sufficiently contiguous, sufficiently connected, sufficiently healthy, uncommitted, and trustworthy resource that a specific workload can actually use within the required time window.**

The runtime intentionally keeps these distinctions first-class:

- nominal capacity is not usable capacity;
- free bytes are not proof that a workload fits;
- idle GPUs are not proof that a distributed workload can be placed;
- a future resource release is not guaranteed capacity;
- a reservation is not forecast capacity - it is a commitment to be subtracted;
- a forecast is not a reservation;
- a capacity estimate is not an allocation;
- a workload being theoretically feasible somewhere does not mean it is currently admissible.

## Systems boundary

Capacity Fabric sits above resource/fleet/topology evidence and beside reservation, scheduling, admission, and resource-broker systems. It models capacity; it does not arbitrate live resources or own live work.

- **Resource Broker** governs real-time scarce-resource arbitration.
- **Reservation Fabric** governs enforceable future capacity commitments.
- **Workload Fabric** governs durable workload lifecycle.
- **Execution Fabric** governs authoritative execution attempts.
- **Preemption Fabric** governs safe interruption and reclaimability.
- **Admission Fabric** may use capacity evidence to decide whether work enters the system.
- **Fragmentation Governor** acts on stranded capacity.
- **SLO Fabric** governs broader service-level obligations.
- Topology Fabric, PCIe Fabric, NUMA Fabric, GPU Fleet Agent, Runtime Registry, and related low-level runtimes provide physical/runtime facts.

Capacity Fabric owns normalized capacity models, current usable-capacity calculation, future capacity projection, workload-demand profiles, headroom modeling, reservation-aware capacity, fragmentation-aware usable capacity, topology/capability-aware capacity, uncertainty and forecast confidence, bottleneck identification, earliest-fit and capacity-window queries, scenario analysis, deterministic explanations, historical forecast evaluation, and conservative recovery of capacity evidence. It does not schedule, broker, reserve, defragment, observe, or run workloads.

## What is implemented

The 1.0.0 runtime (strict /W4 /WX MSVC, C++20, zero project warnings) implements:

- **Strongly typed identities and generations.** Every authority domain has a distinct StrongId/Generation type (resource, fleet, node, device, topology, capability, health, availability, reservation, policy, SLO, scenario, headroom, fragmentation, source/worker boot identity, coordinator epoch, and more). Different generations are never collapsed into generic integers; a stale topology generation cannot satisfy a current capacity query.
- **Multi-dimensional capacity** with strong ByteCount, ByteRate, DeviceCount, ComputeUnits, Duration, Interval, and bounded-fraction types. Invalid values (NaN, infinity, negatives, out-of-range fractions, impossible intervals) are rejected.
- **Explicit evidence provenance** (MEASURED, REPORTED, DERIVED, ESTIMATED, FORECAST, RECONSTRUCTED, SYNTHETIC, UNKNOWN). An UNKNOWN dimension remains unknown and is never converted into usable capacity.
- **Current capacity snapshots** that bind identity, generations, source incarnation, observation time, totals, unavailable, committed, use, and fragmentation.
- **Usable-capacity calculation** over per-dimension availability plus shape-specific constraints (contiguity, topology group, capability class, health, drain, maintenance).
- **Fragmentation modeling** - total free, largest contiguous block, usable-for-shape, stranded memory, fragmentation ratio, and group-level fragmentation. Aggregate sufficiency never implies shape-specific fit.
- **Reservation-aware capacity** that subtracts committed hard reservations over their intervals and exposes the blocking reservation in the explanation.
- **Deterministic forecast model** with explicit conservative / expected / optimistic bounds and horizon-degrading confidence. No invented statistical precision, no fabricated predictive accuracy.
- **Headroom model** distinguishing raw, used, committed, safety, SLO, failure, maintenance, conservative, and workload-specific headroom. Headroom is never collapsed to a single percentage.
- **Feasibility queries** with a deterministic outcome taxonomy (FIT_NOW, FIT_FUTURE, NO_FIT_CAPACITY, NO_FIT_FRAGMENTATION, NO_FIT_TOPOLOGY, NO_FIT_MEMORY, NO_FIT_BANDWIDTH, NO_FIT_RESERVATION, REVALIDATION_REQUIRED, INSUFFICIENT_EVIDENCE, UNKNOWN, and more).
- **Bottleneck explanations** returning typed reasons plus deterministic human-readable text.
- **Earliest-fit search** at event boundaries with a sliding-window minimum, validated against a slow event-grid reference.
- **Scenario analysis** as immutable what-if overlays, always labelled WHAT_IF / SYNTHETIC, never mutating authoritative state.
- **Versioned binary persistence** with magic/version/length framing, an FNV-1a integrity checksum, and rejection of bad magic, unsupported version, truncation, trailing garbage, hostile lengths, checksum mismatches, and duplicate current authority.
- **Conservative recovery after restart**: durable identity, reservations, and forecast history reconstruct; live dynamic evidence becomes REVALIDATION_REQUIRED until fresh workers republish. Current capacity is never asserted from stale/dead worker state.
- **A real multiprocess reference deployment**: a coordinator plus independently-spawned worker OS processes over real loopback TCP with a framed, checksummed, bounded protocol (HELLO, REGISTER, PUBLISH_RESOURCE, PUBLISH_RESERVATION, PUBLISH_RELEASE, QUERY_*, SAVE, SHUTDOWN). Stale boot-identity replay is rejected; worker death invalidates its evidence; a fresh incarnation restores it.

## Building

    cmake -S . -B build -G "Visual Studio 17 2022"
    cmake --build build --config Release
    cmake --build build --config Debug

Required: a C++20-capable toolchain. The reference distributed deployment and the CUDA proof use the Windows SDK (Winsock) and a CUDA toolkit when available.

## Testing

The dependency-free test suite covers units and rejection of malformed values, capacity arithmetic, fragmentation, reservation interaction, forecast uncertainty bounds, earliest-fit against a slow reference, headroom, scenario isolation, persistence and corruption rejection, stale-authority handling, resource-generation advancement, seeded property testing, genuine concurrency, the protocol codec, and the real multiprocess worker-kill/restart and coordinator-restart scenarios. Tests are run without any test timeout.

## CLI and examples

cfcli inspects resources, generations, nominal/usable/committed capacity, headroom, bottlenecks, and earliest fit. The examples/ directory provides runnable demonstrations of current usable capacity, the future timeline, workload fit, earliest fit, reservation-aware capacity, fragmentation, headroom, forecast uncertainty, scenario analysis, stale-source rejection, and persistence/recovery.

## CUDA hardware proof

When a CUDA-capable NVIDIA device is present, the cf_cuda_proof executable discovers the GPU, publishes real device/capacity evidence, measures cudaMemGetInfo, runs real kernels with CPU-reference verification, and demonstrates current-capacity gating (fit, no-fit, fragmentation, live allocation changes, and coordinator restart). It also runs a real worker-death / revalidation scenario: a CUDA-backed worker OS process publishes real RTX 5090 evidence, is killed as a real OS process, its authority is fenced (the query becomes REVALIDATION_REQUIRED), a stale boot replay is rejected, and a fresh incarnation republishes and restores FIT_NOW. It distinguishes physical device capacity, measured free memory, and the governed test pool, and labels evidence REAL, DERIVED, SYNTHETIC, or UNSUPPORTED as appropriate.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.

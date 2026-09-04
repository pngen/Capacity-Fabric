#pragma once

#include "capacity_fabric/core/strong_id.hpp"

namespace capacity_fabric {

// ---- Semantic identity domains ------------------------------------------------
// Each semantic domain has a distinct Tag so that generations and ids of
// different authorities can never be silently interchanged.

// Capacity model
struct CapacityModelIdTag {};
using CapacityModelId = StrongId<CapacityModelIdTag>;
struct CapacityModelGenerationTag {};
using CapacityModelGeneration = Generation<CapacityModelGenerationTag>;

// Capacity snapshot
struct CapacitySnapshotIdTag {};
using CapacitySnapshotId = StrongId<CapacitySnapshotIdTag>;
struct CapacitySnapshotGenerationTag {};
using CapacitySnapshotGeneration = Generation<CapacitySnapshotGenerationTag>;

// Capacity forecast
struct CapacityForecastIdTag {};
using CapacityForecastId = StrongId<CapacityForecastIdTag>;
struct CapacityForecastGenerationTag {};
using CapacityForecastGeneration = Generation<CapacityForecastGenerationTag>;

// Resource
struct ResourceIdTag {};
using ResourceId = StrongId<ResourceIdTag>;
struct ResourceGenerationTag {};
using ResourceGeneration = Generation<ResourceGenerationTag>;

// Resource pool
struct ResourcePoolIdTag {};
using ResourcePoolId = StrongId<ResourcePoolIdTag>;
struct ResourcePoolGenerationTag {};
using ResourcePoolGeneration = Generation<ResourcePoolGenerationTag>;

// Fleet
struct FleetIdTag {};
using FleetId = StrongId<FleetIdTag>;
struct FleetGenerationTag {};
using FleetGeneration = Generation<FleetGenerationTag>;

// Node
struct NodeIdTag {};
using NodeId = StrongId<NodeIdTag>;
struct NodeGenerationTag {};
using NodeGeneration = Generation<NodeGenerationTag>;

// Device
struct DeviceIdTag {};
using DeviceId = StrongId<DeviceIdTag>;
struct DeviceGenerationTag {};
using DeviceGeneration = Generation<DeviceGenerationTag>;

// Memory domain
struct MemoryDomainIdTag {};
using MemoryDomainId = StrongId<MemoryDomainIdTag>;
struct MemoryDomainGenerationTag {};
using MemoryDomainGeneration = Generation<MemoryDomainGenerationTag>;

// Link
struct LinkIdTag {};
using LinkId = StrongId<LinkIdTag>;
struct LinkGenerationTag {};
using LinkGeneration = Generation<LinkGenerationTag>;

// Topology
struct TopologyIdTag {};
using TopologyId = StrongId<TopologyIdTag>;
struct TopologyGenerationTag {};
using TopologyGeneration = Generation<TopologyGenerationTag>;

// Capability
struct CapabilityIdTag {};
using CapabilityId = StrongId<CapabilityIdTag>;
struct CapabilityGenerationTag {};
using CapabilityGeneration = Generation<CapabilityGenerationTag>;

// Health
struct HealthIdTag {};
using HealthId = StrongId<HealthIdTag>;
struct HealthGenerationTag {};
using HealthGeneration = Generation<HealthGenerationTag>;

// Availability
struct AvailabilityIdTag {};
using AvailabilityId = StrongId<AvailabilityIdTag>;
struct AvailabilityGenerationTag {};
using AvailabilityGeneration = Generation<AvailabilityGenerationTag>;

// Reservation (owned by Reservation Fabric; consumed as an external fact)
struct ReservationIdTag {};
using ReservationId = StrongId<ReservationIdTag>;
struct ReservationGenerationTag {};
using ReservationGeneration = Generation<ReservationGenerationTag>;

// Workload demand
struct WorkloadDemandIdTag {};
using WorkloadDemandId = StrongId<WorkloadDemandIdTag>;
struct WorkloadDemandGenerationTag {};
using WorkloadDemandGeneration = Generation<WorkloadDemandGenerationTag>;

// Policy
struct PolicyIdTag {};
using PolicyId = StrongId<PolicyIdTag>;
struct PolicyGenerationTag {};
using PolicyGeneration = Generation<PolicyGenerationTag>;

// SLO
struct SloIdTag {};
using SloId = StrongId<SloIdTag>;
struct SloGenerationTag {};
using SloGeneration = Generation<SloGenerationTag>;

// Forecast source
struct ForecastSourceIdTag {};
using ForecastSourceId = StrongId<ForecastSourceIdTag>;
struct ForecastSourceGenerationTag {};
using ForecastSourceGeneration = Generation<ForecastSourceGenerationTag>;

// Source incarnation / boot identity
struct SourceBootIdTag {};
using SourceBootId = StrongId<SourceBootIdTag>;
struct WorkerBootIdTag {};
using WorkerBootId = StrongId<WorkerBootIdTag>;
struct WorkerIdTag {};
using WorkerId = StrongId<WorkerIdTag>;

// Coordinator epoch
struct CoordinatorEpochTag {};
using CoordinatorEpoch = Generation<CoordinatorEpochTag>;

// Scenario
struct ScenarioIdTag {};
using ScenarioId = StrongId<ScenarioIdTag>;
struct ScenarioGenerationTag {};
using ScenarioGeneration = Generation<ScenarioGenerationTag>;

// Headroom & fragmentation
struct HeadroomIdTag {};
using HeadroomId = StrongId<HeadroomIdTag>;
struct HeadroomGenerationTag {};
using HeadroomGeneration = Generation<HeadroomGenerationTag>;
struct FragmentationIdTag {};
using FragmentationId = StrongId<FragmentationIdTag>;
struct FragmentationGenerationTag {};
using FragmentationGeneration = Generation<FragmentationGenerationTag>;

// Authority
struct AuthorityIdTag {};
using AuthorityId = StrongId<AuthorityIdTag>;
struct AuthorityGenerationTag {};
using AuthorityGeneration = Generation<AuthorityGenerationTag>;

} // namespace capacity_fabric

#pragma once

#include <cstdint>
#include <string_view>

namespace capacity_fabric {

// Multi-dimensional capacity model. Capacity is never reduced to a single
// accelerator count; a workload can be limited by memory, contiguity, topology,
// bandwidth, residency, or any other dimension.
enum class Dimension : uint8_t {
    AcceleratorCount,
    AcceleratorCompute,       // normalized compute share (0..N)
    VramBytes,                // aggregate device VRAM
    ContiguousVramBytes,      // largest free contiguous device VRAM block
    PinnedHostMemoryBytes,
    PageableHostMemoryBytes,
    CpuCount,
    StorageBytes,
    StorageBandwidth,         // bytes/second
    PcieBandwidth,            // bytes/second
    NetworkBandwidth,         // bytes/second
    CollectiveBandwidth,      // bytes/second
    QueueSlots,
    ModelResidencySlots,
    AdapterResidencySlots,
    TensorStateResidencyBytes,
    TransferBudgetBytes,
    PowerHeadroom,            // watts
    NodeCount,
    RackLocalCount,           // max devices in one rack/failure domain
    TopologyLocalCount,       // max devices in one topology-local group
};

// Human-readable name for a dimension.
inline const char* dimensionName(Dimension d) noexcept {
    switch (d) {
        case Dimension::AcceleratorCount:        return "accelerator_count";
        case Dimension::AcceleratorCompute:      return "accelerator_compute";
        case Dimension::VramBytes:               return "vram_bytes";
        case Dimension::ContiguousVramBytes:     return "contiguous_vram_bytes";
        case Dimension::PinnedHostMemoryBytes:   return "pinned_host_memory_bytes";
        case Dimension::PageableHostMemoryBytes: return "pageable_host_memory_bytes";
        case Dimension::CpuCount:                return "cpu_count";
        case Dimension::StorageBytes:            return "storage_bytes";
        case Dimension::StorageBandwidth:        return "storage_bandwidth";
        case Dimension::PcieBandwidth:           return "pcie_bandwidth";
        case Dimension::NetworkBandwidth:        return "network_bandwidth";
        case Dimension::CollectiveBandwidth:     return "collective_bandwidth";
        case Dimension::QueueSlots:              return "queue_slots";
        case Dimension::ModelResidencySlots:     return "model_residency_slots";
        case Dimension::AdapterResidencySlots:   return "adapter_residency_slots";
        case Dimension::TensorStateResidencyBytes: return "tensor_state_residency_bytes";
        case Dimension::TransferBudgetBytes:     return "transfer_budget_bytes";
        case Dimension::PowerHeadroom:           return "power_headroom";
        case Dimension::NodeCount:               return "node_count";
        case Dimension::RackLocalCount:          return "rack_local_count";
        case Dimension::TopologyLocalCount:      return "topology_local_count";
    }
    return "unknown";
}

// True for dimensions whose natural unit is bytes.
inline constexpr bool dimensionIsBytes(Dimension d) noexcept {
    switch (d) {
        case Dimension::VramBytes:
        case Dimension::ContiguousVramBytes:
        case Dimension::PinnedHostMemoryBytes:
        case Dimension::PageableHostMemoryBytes:
        case Dimension::StorageBytes:
        case Dimension::TensorStateResidencyBytes:
        case Dimension::TransferBudgetBytes:
            return true;
        default:
            return false;
    }
}

// True for dimensions whose natural unit is bytes/second.
inline constexpr bool dimensionIsRate(Dimension d) noexcept {
    switch (d) {
        case Dimension::StorageBandwidth:
        case Dimension::PcieBandwidth:
        case Dimension::NetworkBandwidth:
        case Dimension::CollectiveBandwidth:
            return true;
        default:
            return false;
    }
}

// True for count-like dimensions.
inline constexpr bool dimensionIsCount(Dimension d) noexcept {
    switch (d) {
        case Dimension::AcceleratorCount:
        case Dimension::CpuCount:
        case Dimension::QueueSlots:
        case Dimension::ModelResidencySlots:
        case Dimension::AdapterResidencySlots:
        case Dimension::NodeCount:
        case Dimension::RackLocalCount:
        case Dimension::TopologyLocalCount:
            return true;
        default:
            return false;
    }
}

} // namespace capacity_fabric

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>

#include "capacity_fabric/capacity/dimension.hpp"
#include "capacity_fabric/core/units.hpp"

namespace capacity_fabric {

// A validated multi-dimensional capacity amount. Each dimension is stored as a
// non-negative finite double in that dimension's natural unit (bytes for memory
// dimensions, counts for count dimensions, bytes/second for rate dimensions).
// The typed accessors convert to and from the strong unit wrappers.
class Capacity {
public:
    Capacity() = default;

    [[nodiscard]] bool has(Dimension d) const { return amounts_.contains(d); }

    [[nodiscard]] double amount(Dimension d) const {
        auto it = amounts_.find(d);
        return it == amounts_.end() ? 0.0 : it->second;
    }

    // Sets an amount, validating it against the dimension's unit rules.
    // Returns false (and leaves the model unchanged) on invalid input.
    bool setAmount(Dimension d, double v) {
        if (!std::isfinite(v) || v < 0.0) {
            return false;
        }
        if (v == 0.0) {
            amounts_.erase(d);
        } else {
            amounts_[d] = v;
        }
        return true;
    }

    // Adds another capacity component-wise (saturating at double max is not
    // expected; validation rejects non-finite results).
    void add(const Capacity& other) {
        for (const auto& [d, v] : other.amounts_) {
            const double cur = amount(d);
            const double sum = cur + v;
            if (!std::isfinite(sum)) {
                throw std::overflow_error("Capacity::add: non-finite sum");
            }
            amounts_[d] = sum;
        }
    }

    // Subtracts component-wise, clamping at zero so capacity never goes negative.
    void subtractClamp(const Capacity& other) {
        for (const auto& [d, v] : other.amounts_) {
            const double cur = amount(d);
            const double t = cur - v;
            amounts_[d] = (t > 0.0) ? t : 0.0;
        }
    }

    // Attempts an exact (non-clamping) subtract. Returns false (and leaves the
    // model unchanged) if any component would go negative.
    bool trySubtract(const Capacity& other) {
        for (const auto& [d, v] : other.amounts_) {
            const double cur = amount(d);
            if (cur < v) {
                return false;
            }
        }
        for (const auto& [d, v] : other.amounts_) {
            amounts_[d] = amount(d) - v;
            if (amounts_[d] == 0.0) amounts_.erase(d);
        }
        return true;
    }

    // Does this capacity satisfy (>=) every component of the requirement?
    [[nodiscard]] bool satisfies(const Capacity& requirement) const {
        for (const auto& [d, v] : requirement.amounts_) {
            if (amount(d) < v) {
                return false;
            }
        }
        return true;
    }

    // Dimensions where this is strictly less than the requirement.
    [[nodiscard]] std::vector<Dimension> missingDimensions(const Capacity& requirement) const {
        std::vector<Dimension> out;
        for (const auto& [d, v] : requirement.amounts_) {
            if (amount(d) < v) {
                out.push_back(d);
            }
        }
        return out;
    }

    // The component-wise minimum of this and other.
    [[nodiscard]] Capacity min(const Capacity& other) const {
        Capacity out;
        for (const auto& [d, v] : amounts_) {
            const double ov = other.amount(d);
            out.amounts_[d] = (v < ov) ? v : ov;
        }
        for (const auto& [d, v] : other.amounts_) {
            if (!out.has(d)) out.amounts_[d] = v;
        }
        return out;
    }

    // The component-wise maximum of this and other.
    [[nodiscard]] Capacity maxCap(const Capacity& other) const {
        Capacity out;
        for (const auto& [d, v] : amounts_) {
            const double ov = other.amount(d);
            out.amounts_[d] = (v > ov) ? v : ov;
        }
        for (const auto& [d, v] : other.amounts_) {
            if (!out.has(d)) out.amounts_[d] = v;
        }
        return out;
    }

    [[nodiscard]] const auto& entries() const { return amounts_; }

    // ---- Typed accessors ------------------------------------------------------
    void setVram(ByteCount b) { setAmount(Dimension::VramBytes, static_cast<double>(b.value())); }
    void setVramContiguous(ByteCount b) { setAmount(Dimension::ContiguousVramBytes, static_cast<double>(b.value())); }
    void setPinnedHostMemory(ByteCount b) { setAmount(Dimension::PinnedHostMemoryBytes, static_cast<double>(b.value())); }
    void setPageableHostMemory(ByteCount b) { setAmount(Dimension::PageableHostMemoryBytes, static_cast<double>(b.value())); }
    void setStorage(ByteCount b) { setAmount(Dimension::StorageBytes, static_cast<double>(b.value())); }
    void setTensorStateResidency(ByteCount b) { setAmount(Dimension::TensorStateResidencyBytes, static_cast<double>(b.value())); }
    void setTransferBudget(ByteCount b) { setAmount(Dimension::TransferBudgetBytes, static_cast<double>(b.value())); }

    void setAccelerators(DeviceCount c) { setAmount(Dimension::AcceleratorCount, static_cast<double>(c.value())); }
    void setCpuCount(DeviceCount c) { setAmount(Dimension::CpuCount, static_cast<double>(c.value())); }
    void setQueueSlots(DeviceCount c) { setAmount(Dimension::QueueSlots, static_cast<double>(c.value())); }
    void setModelResidencySlots(DeviceCount c) { setAmount(Dimension::ModelResidencySlots, static_cast<double>(c.value())); }
    void setAdapterResidencySlots(DeviceCount c) { setAmount(Dimension::AdapterResidencySlots, static_cast<double>(c.value())); }
    void setNodeCount(DeviceCount c) { setAmount(Dimension::NodeCount, static_cast<double>(c.value())); }
    void setRackLocalCount(DeviceCount c) { setAmount(Dimension::RackLocalCount, static_cast<double>(c.value())); }
    void setTopologyLocalCount(DeviceCount c) { setAmount(Dimension::TopologyLocalCount, static_cast<double>(c.value())); }

    void setCompute(ComputeUnits c) { setAmount(Dimension::AcceleratorCompute, c.value()); }
    void setStorageBandwidth(ByteRate r) { setAmount(Dimension::StorageBandwidth, r.value()); }
    void setPcieBandwidth(ByteRate r) { setAmount(Dimension::PcieBandwidth, r.value()); }
    void setNetworkBandwidth(ByteRate r) { setAmount(Dimension::NetworkBandwidth, r.value()); }
    void setCollectiveBandwidth(ByteRate r) { setAmount(Dimension::CollectiveBandwidth, r.value()); }
    void setPowerHeadroom(Power p) { setAmount(Dimension::PowerHeadroom, p.value()); }

    [[nodiscard]] ByteCount vram() const { return ByteCount(static_cast<uint64_t>(amount(Dimension::VramBytes))); }
    [[nodiscard]] ByteCount vramContiguous() const { return ByteCount(static_cast<uint64_t>(amount(Dimension::ContiguousVramBytes))); }
    [[nodiscard]] DeviceCount accelerators() const { return DeviceCount(static_cast<uint64_t>(amount(Dimension::AcceleratorCount))); }

    friend bool operator==(const Capacity& a, const Capacity& b) { return a.amounts_ == b.amounts_; }
    friend std::ostream& operator<<(std::ostream& os, const Capacity& c) {
        bool first = true;
        os << "{";
        for (const auto& [d, v] : c.amounts_) {
            if (!first) os << ", ";
            os << dimensionName(d) << "=" << v;
            first = false;
        }
        os << "}";
        return os;
    }

private:
    std::map<Dimension, double> amounts_;
};

} // namespace capacity_fabric

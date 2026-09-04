#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Bridge between the pure-C++ Capacity Fabric host code and the nvcc-compiled
// CUDA proof. Only this narrow interface touches CUDA runtime APIs.
namespace capacity_fabric::cuda {

struct DeviceInfo {
    bool present = false;
    int ordinal = -1;
    std::string name;
    int computeMajor = 0;
    int computeMinor = 0;
    std::size_t totalBytes = 0;
    std::size_t freeBytes = 0;
    bool supported = true;   // false when CUDA is unavailable or the device is absent
};

// Discovers the first CUDA-capable device and measures current free/total memory.
bool probeDevice(DeviceInfo& out, std::string& err);

// Measures the current free device memory.
bool currentFreeBytes(std::size_t& freeBytes, std::string& err);

// Allocates a device buffer. Returns false on error (e.g. out of memory).
bool allocate(std::size_t bytes, void** ptr, std::string& err);

// Frees a device buffer.
bool release(void* ptr, std::string& err);

// Runs a real CUDA kernel over N floats (out[i] = in[i] + 1.0f), copies the
// result back, and verifies it against a CPU reference. Returns true on parity.
bool kernelAndVerify(const float* input, std::size_t count, std::string& err);

} // namespace capacity_fabric::cuda

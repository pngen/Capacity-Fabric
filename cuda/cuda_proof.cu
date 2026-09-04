#include "cuda_bridge.h"

#include <cuda_runtime.h>
#include <cstdio>
#include <string>
#include <vector>

namespace capacity_fabric::cuda {

namespace {
const char* errString(cudaError_t e) { return cudaGetErrorString(e); }
}

bool probeDevice(DeviceInfo& out, std::string& err) {
    int count = 0;
    cudaError_t e = cudaGetDeviceCount(&count);
    if (e != cudaSuccess || count <= 0) {
        err = errString(e);
        out.present = false;
        return false;
    }
    if (cudaSetDevice(0) != cudaSuccess) { err = errString(cudaGetLastError()); out.present = false; return false; }
    cudaDeviceProp prop{};
    if (cudaGetDeviceProperties(&prop, 0) != cudaSuccess) { err = errString(cudaGetLastError()); out.present = false; return false; }
    out.present = true;
    out.ordinal = 0;
    out.name = prop.name;
    out.computeMajor = prop.major;
    out.computeMinor = prop.minor;
    size_t freeBytes = 0, totalBytes = 0;
    if (cudaMemGetInfo(&freeBytes, &totalBytes) != cudaSuccess) { err = errString(cudaGetLastError()); out.freeBytes = 0; out.totalBytes = prop.totalGlobalMem; return true; }
    out.freeBytes = freeBytes;
    out.totalBytes = totalBytes ? totalBytes : prop.totalGlobalMem;
    return true;
}

bool currentFreeBytes(std::size_t& freeBytes, std::string& err) {
    size_t f = 0, t = 0;
    if (cudaMemGetInfo(&f, &t) != cudaSuccess) { err = errString(cudaGetLastError()); return false; }
    freeBytes = f;
    return true;
}

bool allocate(std::size_t bytes, void** ptr, std::string& err) {
    if (cudaMalloc(ptr, bytes) != cudaSuccess) { err = errString(cudaGetLastError()); return false; }
    return true;
}

bool release(void* ptr, std::string& err) {
    if (cudaFree(ptr) != cudaSuccess) { err = errString(cudaGetLastError()); return false; }
    return true;
}

__global__ void addOne(float* data, std::size_t n) {
    std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) data[i] += 1.0f;
}

bool kernelAndVerify(const float* input, std::size_t count, std::string& err) {
    if (count == 0) return true;
    float* dev = nullptr;
    if (cudaMalloc(&dev, count * sizeof(float)) != cudaSuccess) { err = errString(cudaGetLastError()); return false; }
    if (cudaMemcpy(dev, input, count * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess) {
        err = errString(cudaGetLastError()); cudaFree(dev); return false;
    }
    const int block = 256;
    const int grid = static_cast<int>((count + block - 1) / block);
    addOne<<<grid, block>>>(dev, count);
    if (cudaGetLastError() != cudaSuccess) { err = errString(cudaGetLastError()); cudaFree(dev); return false; }
    if (cudaDeviceSynchronize() != cudaSuccess) { err = errString(cudaGetLastError()); cudaFree(dev); return false; }
    std::vector<float> out(count);
    if (cudaMemcpy(out.data(), dev, count * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) {
        err = errString(cudaGetLastError()); cudaFree(dev); return false;
    }
    cudaFree(dev);
    // CPU reference verification.
    for (std::size_t i = 0; i < count; ++i) {
        if (out[i] != input[i] + 1.0f) { err = "CPU parity mismatch at " + std::to_string(i); return false; }
    }
    return true;
}

} // namespace capacity_fabric::cuda

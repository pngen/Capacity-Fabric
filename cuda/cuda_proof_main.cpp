#include <cstdio>

namespace capacity_fabric::cuda {
int runCudaProof();
}

int main() {
    std::printf("=== Capacity Fabric CUDA hardware proof (RTX 5090 / sm_120) ===\n");
    const int rc = capacity_fabric::cuda::runCudaProof();
    std::printf("=== END ===\n");
    return rc;
}

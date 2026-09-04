#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "capacity_fabric/coordinator/coordinator.hpp"
#include "capacity_fabric/model/model.hpp"

// Reference coordinator executable. Usage: cfcoord <port> [statePath]
// Prints "PORT:<n>" after binding so the driver can learn an ephemeral port.
int main(int argc, char** argv) {
    uint16_t port = 0;
    std::string statePath;
    if (argc >= 2) port = static_cast<uint16_t>(std::stoi(argv[1]));
    if (argc >= 3) statePath = argv[2];

    capacity_fabric::CapacityModel model;
    capacity_fabric::Coordinator coord(model);
    std::string err;
    const auto bound = coord.start(port, statePath, err);
    if (!bound.has_value()) {
        std::cerr << "cfcoord: failed to bind: " << err << std::endl;
        return 1;
    }
    std::cout << "PORT:" << *bound << std::endl;
    std::cout.flush();

    while (coord.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    coord.stop();
    return 0;
}

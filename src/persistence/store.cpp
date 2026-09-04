#include "capacity_fabric/persistence/store.hpp"

#include <fstream>
#include <iterator>

namespace capacity_fabric {

bool writeFileBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool readFileBytes(const std::string& path, std::vector<uint8_t>& bytes, std::size_t maxBytes) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto size = in.tellg();
    if (size < 0) return false;
    if (static_cast<std::size_t>(size) > maxBytes) return false;
    bytes.resize(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(in);
}

} // namespace capacity_fabric

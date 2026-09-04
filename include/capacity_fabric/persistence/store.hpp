#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace capacity_fabric {

// Writes a byte buffer to a file (binary). Returns false on IO failure.
bool writeFileBytes(const std::string& path, const std::vector<uint8_t>& bytes);

// Reads a whole file into a byte buffer. Returns false on failure or if the file
// is larger than maxBytes (a hostile-size guard).
bool readFileBytes(const std::string& path, std::vector<uint8_t>& bytes,
                   std::size_t maxBytes = 64u * 1024u * 1024u);

} // namespace capacity_fabric

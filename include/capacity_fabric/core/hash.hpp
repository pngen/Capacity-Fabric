#pragma once

#include <cstddef>
#include <functional>

#include "capacity_fabric/core/strong_id.hpp"

namespace std {

template <class Tag, uint64_t Invalid>
struct hash<::capacity_fabric::StrongId<Tag, Invalid>> {
    size_t operator()(const ::capacity_fabric::StrongId<Tag, Invalid>& id) const noexcept {
        return std::hash<uint64_t>{}(id.value());
    }
};

template <class Tag, uint64_t Invalid>
struct hash<::capacity_fabric::Generation<Tag, Invalid>> {
    size_t operator()(const ::capacity_fabric::Generation<Tag, Invalid>& g) const noexcept {
        return std::hash<uint64_t>{}(g.value());
    }
};

} // namespace std

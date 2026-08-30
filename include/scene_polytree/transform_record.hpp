#pragma once

#include <cstdint>

namespace scene_polytree
{
    template<class Transform>
    struct transform_record
    {
        Transform local{};
        Transform world{};
        std::uint64_t local_revision{};
        std::uint64_t world_revision{};
        bool dirty{true};
    };
}

#pragma once

namespace scene_polytree::motion
{
    template<class LinearVelocity, class AngularVelocity>
    struct motion_state
    {
        LinearVelocity linear_velocity{};
        AngularVelocity angular_velocity{};
        bool enabled{true};
    };
}

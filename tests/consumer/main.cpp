#include <scene_polytree/motion/motion.hpp>
#include <scene_polytree/scene_polytree.hpp>

#include <chrono>
#include <cstdint>
#include <utility>

namespace {
struct translation {
    int value{};
};

struct translation_policy {
    using transform_type = translation;
    using linear_velocity_type = int;
    using angular_velocity_type = int;

    [[nodiscard]] translation compose(const translation &parent_world,
                                      const translation &local) noexcept {
        return {parent_world.value + local.value};
    }

    [[nodiscard]] translation integrate(const translation &local,
                                        const scene_polytree::motion::motion_state<int, int> &state,
                                        scene_polytree::motion::fixed_motion_step step) noexcept {
        const auto seconds =
            static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(step.delta).count());
        return {local.value + state.linear_velocity * seconds};
    }

    [[nodiscard]] bool
    is_stationary(const scene_polytree::motion::motion_state<int, int> &state) noexcept {
        return state.linear_velocity == 0 && state.angular_velocity == 0;
    }
};

static_assert(scene_polytree::TransformPolicy<translation_policy>);
static_assert(scene_polytree::motion::MotionPolicy<translation_policy>);
} // namespace

int main() {
    scene_polytree::basic_authoring_scene<std::uint32_t, std::uint32_t, translation> authoring;
    const auto root = authoring.insert_root(1u, translation{10});
    if (!root) {
        return 1;
    }

    wz::core::graph::FreezeWorkspace freeze_workspace;
    auto frozen = scene_polytree::freeze_scene(authoring, freeze_workspace);
    if (!frozen) {
        return 2;
    }

    auto runtime = std::move(frozen).value();
    const auto runtime_root = runtime.identities().runtime_handle(root.value());
    if (!runtime_root) {
        return 3;
    }

    scene_polytree::motion::active_motion_set<int, int> active{runtime.topology()};
    translation_policy policy;
    if (active.set(runtime_root.value(), {2, 0}, policy) !=
        scene_polytree::motion::motion_error::none) {
        return 4;
    }

    scene_polytree::motion::fixed_step_sequence sequence{std::chrono::seconds{1}};
    scene_polytree::motion::motion_evaluation_workspace<translation> motion_workspace;
    scene_polytree::transform_evaluation_workspace transform_workspace;
    const auto result = scene_polytree::motion::advance_motion_scene(
        runtime.topology(), runtime.state(), active, sequence, motion_workspace,
        transform_workspace, policy, policy);
    return result && runtime.state().local(runtime_root.value()).value == 12 &&
                   result.integrated_nodes.size() == 1 && sequence.next_tick() == 1
               ? 0
               : 5;
}

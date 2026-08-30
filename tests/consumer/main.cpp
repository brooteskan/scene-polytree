#include <scene_polytree/scene_polytree.hpp>

#include <cstdint>
#include <utility>

namespace {
struct translation {
    int value{};
};

struct translation_policy {
    using transform_type = translation;

    [[nodiscard]] translation compose(const translation &parent_world,
                                      const translation &local) noexcept {
        return {parent_world.value + local.value};
    }
};

static_assert(scene_polytree::TransformPolicy<translation_policy>);
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
    scene_polytree::transform_evaluation_workspace evaluation_workspace;
    auto plan = scene_polytree::make_transform_evaluation_plan(runtime.topology(), runtime.state(),
                                                               evaluation_workspace);
    translation_policy policy;
    return scene_polytree::evaluate_transforms(runtime.topology(), runtime.state(), plan.value(),
                                               policy)
               ? 0
               : 3;
}

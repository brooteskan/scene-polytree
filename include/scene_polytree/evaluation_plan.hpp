#pragma once

#include <graph/static_polytree.h>

namespace scene_polytree
{
    // Legacy type spelling for v0.1.0 consumers. New code should call
    // wz::core::graph::evaluation_plan directly so scene-polytree does not own
    // a second scheduling contract. The generic plan's field names are the
    // authoritative API.
    template<class NodeHandle>
    using evaluation_plan_view = wz::core::graph::PolytreeEvaluationPlan;
}

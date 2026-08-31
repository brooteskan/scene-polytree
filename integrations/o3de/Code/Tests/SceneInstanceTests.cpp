#include "SceneInstance.h"

#include <AzTest/AzTest.h>

#include <chrono>
#include <ranges>
#include <vector>

namespace ScenePolytree::Tests {
namespace {
[[nodiscard]] AZStd::vector<ScenePolytreeNodeDescriptor> MakeTopology() {
    return {
        {AZ::Name("Root"), AZ::Name(), ScenePolytreeNodeType::Transform,
         ScenePolytreeJointType::None, AZ::Transform::CreateIdentity()},
        {AZ::Name("Child"), AZ::Name("Root"), ScenePolytreeNodeType::Transform,
         ScenePolytreeJointType::Fixed,
         AZ::Transform::CreateTranslation(AZ::Vector3::CreateAxisZ())},
    };
}

struct WrittenTransform {
    AZ::EntityId m_entity;
    AZ::Transform m_world;
};

[[nodiscard]] const AZ::Transform *FindWritten(const std::vector<WrittenTransform> &written,
                                               AZ::EntityId entity) {
    const auto found = std::ranges::find(written, entity, &WrittenTransform::m_entity);
    return found != written.end() ? &found->m_world : nullptr;
}
} // namespace

TEST(ScenePolytreeSceneInstanceTests, SharedPartitionsReserveAndResolveIsolatedSlots) {
    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_partitions = {
        {11, 2, MakeTopology()},
        {22, 1, MakeTopology()},
    };
    auto scene = Internal::SceneInstance::Create(descriptor);
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->GetStatistics().m_nodeCount, 6);
    EXPECT_EQ(scene->GetStatistics().m_partitionCount, 2);
    EXPECT_EQ(scene->GetStatistics().m_slotCapacity, 3);

    const SceneHandle sceneHandle{77};
    const SlotResult first = scene->ReserveSlot({sceneHandle, 11, 1});
    const SlotResult second = scene->ReserveSlot({sceneHandle, 22, 1});
    ASSERT_TRUE(first.IsSuccess());
    ASSERT_TRUE(second.IsSuccess());
    EXPECT_TRUE(scene->ResolveNode(first.m_handle, AZ::Name("Child")).IsSuccess());
    EXPECT_TRUE(scene->ResolveNode(second.m_handle, AZ::Name("Child")).IsSuccess());
    EXPECT_EQ(scene->GetStatistics().m_reservedSlotCount, 2);
}

TEST(ScenePolytreeSceneInstanceTests, PlacementBindingAndGenericCorrectionProjectTransforms) {
    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_partitions = {{31, 1, MakeTopology()}};
    auto scene = Internal::SceneInstance::Create(descriptor);
    ASSERT_NE(scene, nullptr);

    const SlotResult slot = scene->ReserveSlot({SceneHandle{88}, 31, 1});
    ASSERT_TRUE(slot.IsSuccess());
    const AZ::Transform placement =
        AZ::Transform::CreateTranslation(AZ::Vector3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(scene->PlaceSlot(slot.m_handle, placement), ScenePolytreeResultCode::Success);
    EXPECT_EQ(scene->BindSlot(
                  slot.m_handle,
                  {
                      {AZ::Name("Root"), AZ::EntityId(1001), AZ::Transform::CreateIdentity()},
                      {AZ::Name("Child"), AZ::EntityId(1002), AZ::Transform::CreateIdentity()},
                  }),
              ScenePolytreeResultCode::Success);

    std::vector<WrittenTransform> initial;
    scene->Advance(std::chrono::nanoseconds::zero(),
                   [&](AZ::EntityId entity, const AZ::Transform &world) {
                       initial.push_back({entity, world});
                   });
    ASSERT_EQ(initial.size(), 2);
    ASSERT_NE(FindWritten(initial, AZ::EntityId(1001)), nullptr);
    ASSERT_NE(FindWritten(initial, AZ::EntityId(1002)), nullptr);
    EXPECT_TRUE(FindWritten(initial, AZ::EntityId(1001))->IsClose(placement));
    EXPECT_TRUE(FindWritten(initial, AZ::EntityId(1002))
                    ->GetTranslation()
                    .IsClose(AZ::Vector3(10.0f, 20.0f, 31.0f)));

    const NodeResult child = scene->ResolveNode(slot.m_handle, AZ::Name("Child"));
    ASSERT_TRUE(child.IsSuccess());
    EXPECT_TRUE(scene->CorrectLocal(
        child.m_handle, AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 0.0f, 2.0f))));
    std::vector<WrittenTransform> corrected;
    scene->Advance(std::chrono::nanoseconds::zero(),
                   [&](AZ::EntityId entity, const AZ::Transform &world) {
                       corrected.push_back({entity, world});
                   });
    ASSERT_EQ(corrected.size(), 1);
    ASSERT_NE(FindWritten(corrected, AZ::EntityId(1002)), nullptr);
    EXPECT_TRUE(FindWritten(corrected, AZ::EntityId(1002))
                    ->GetTranslation()
                    .IsClose(AZ::Vector3(10.0f, 20.0f, 32.0f)));
}

TEST(ScenePolytreeSceneInstanceTests, DuplicateEntityBindingsAreRejectedAcrossPartitions) {
    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_partitions = {
        {41, 1, MakeTopology()},
        {42, 1, MakeTopology()},
    };
    auto scene = Internal::SceneInstance::Create(descriptor);
    ASSERT_NE(scene, nullptr);
    const SceneHandle sceneHandle{99};
    const SlotResult first = scene->ReserveSlot({sceneHandle, 41, 1});
    const SlotResult second = scene->ReserveSlot({sceneHandle, 42, 1});
    ASSERT_TRUE(first.IsSuccess());
    ASSERT_TRUE(second.IsSuccess());

    const AZStd::vector<ScenePolytreeEntityBinding> bindings{
        {AZ::Name("Root"), AZ::EntityId(2001), AZ::Transform::CreateIdentity()},
        {AZ::Name("Child"), AZ::EntityId(2002), AZ::Transform::CreateIdentity()},
    };
    EXPECT_EQ(scene->BindSlot(first.m_handle, bindings), ScenePolytreeResultCode::Success);
    EXPECT_EQ(scene->BindSlot(second.m_handle, bindings), ScenePolytreeResultCode::InvalidBinding);
}

TEST(ScenePolytreeSceneInstanceTests, ReleasedSlotsInvalidateOldHandlesAndCanBeReused) {
    ScenePolytreeSceneDescriptor descriptor;
    descriptor.m_partitions = {{51, 1, MakeTopology()}};
    auto scene = Internal::SceneInstance::Create(descriptor);
    ASSERT_NE(scene, nullptr);
    const SpawnerHandle spawner{SceneHandle{101}, 51, 1};
    const SlotResult first = scene->ReserveSlot(spawner);
    ASSERT_TRUE(first.IsSuccess());
    EXPECT_EQ(scene->ReleaseSlot(first.m_handle), ScenePolytreeResultCode::Success);
    EXPECT_FALSE(scene->ResolveNode(first.m_handle, AZ::Name("Root")).IsSuccess());

    const SlotResult second = scene->ReserveSlot(spawner);
    ASSERT_TRUE(second.IsSuccess());
    EXPECT_EQ(second.m_handle.m_slot, first.m_handle.m_slot);
    EXPECT_NE(second.m_handle.m_generation, first.m_handle.m_generation);
}
} // namespace ScenePolytree::Tests

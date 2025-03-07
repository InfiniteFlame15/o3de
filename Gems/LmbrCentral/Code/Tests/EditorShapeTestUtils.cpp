/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <EditorShapeTestUtils.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <LmbrCentral/Shape/BoxShapeComponentBus.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>
#include <Shape/EditorSphereShapeComponent.h>

namespace LmbrCentral
{
    // use a large tolerance for manipulator tests, because accuracy is limited by viewport resolution
    static constexpr float ManipulatorTolerance = 0.1f;

    void DragMouse(
        const AzFramework::CameraState& cameraState,
        AzManipulatorTestFramework::ImmediateModeActionDispatcher* actionDispatcher,
        const AZ::Vector3& worldStart,
        const AZ::Vector3& worldEnd,
        const AzToolsFramework::ViewportInteraction::KeyboardModifier keyboardModifier)
    {
        const auto screenStart = AzFramework::WorldToScreen(worldStart, cameraState);
        const auto screenEnd = AzFramework::WorldToScreen(worldEnd, cameraState);

        actionDispatcher
            ->CameraState(cameraState)
            ->MousePosition(screenStart)
            ->KeyboardModifierDown(keyboardModifier)
            ->MouseLButtonDown()
            ->MousePosition(screenEnd)
            ->MouseLButtonUp();
    }

    void EnterComponentMode(AZ::EntityId entityId, const AZ::Uuid& componentType)
    {
        AzToolsFramework::SelectEntity(entityId);
        AzToolsFramework::ComponentModeFramework::ComponentModeSystemRequestBus::Broadcast(
            &AzToolsFramework::ComponentModeFramework::ComponentModeSystemRequestBus::Events::AddSelectedComponentModesOfType,
            componentType);
    }

    void ExpectBoxDimensions(AZ::EntityId entityId, const AZ::Vector3& expectedBoxDimensions)
    {
        AZ::Vector3 boxDimensions = AZ::Vector3::CreateZero();
        BoxShapeComponentRequestsBus::EventResult(boxDimensions, entityId, &BoxShapeComponentRequests::GetBoxDimensions);
        EXPECT_THAT(boxDimensions, UnitTest::IsCloseTolerance(expectedBoxDimensions, ManipulatorTolerance));
    }

    void ExpectTranslationOffset(AZ::EntityId entityId, const AZ::Vector3& expectedTranslationOffset)
    {
        AZ::Vector3 translationOffset = AZ::Vector3::CreateZero();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            translationOffset, entityId, &LmbrCentral::ShapeComponentRequests::GetTranslationOffset);
        EXPECT_THAT(translationOffset, UnitTest::IsCloseTolerance(expectedTranslationOffset, ManipulatorTolerance));
    }

    void SetComponentSubMode(AZ::EntityComponentIdPair entityComponentIdPair, AzToolsFramework::ShapeComponentModeRequests::SubMode subMode)
    {
        AzToolsFramework::ShapeComponentModeRequestBus::Event(
            entityComponentIdPair, &AzToolsFramework::ShapeComponentModeRequests::SetShapeSubMode, subMode);
    }

    void ExpectSubMode(
        AZ::EntityComponentIdPair entityComponentIdPair, AzToolsFramework::ShapeComponentModeRequests::SubMode expectedSubMode)
    {
        AzToolsFramework::ShapeComponentModeRequests::SubMode subMode = AzToolsFramework::ShapeComponentModeRequests::SubMode::NumModes;
        AzToolsFramework::ShapeComponentModeRequestBus::EventResult(
            subMode, entityComponentIdPair, &AzToolsFramework::ShapeComponentModeRequests::GetShapeSubMode);
        EXPECT_EQ(subMode, expectedSubMode);
    }

    AzToolsFramework::ViewportInteraction::MouseInteractionResult CtrlScroll(float wheelDelta)
    {
        AzToolsFramework::ViewportInteraction::MouseInteractionEvent interactionEvent(
            AzToolsFramework::ViewportInteraction::MouseInteraction(), wheelDelta);
        interactionEvent.m_mouseEvent = AzToolsFramework::ViewportInteraction::MouseEvent::Wheel;
        interactionEvent.m_mouseInteraction.m_keyboardModifiers.m_keyModifiers =
            static_cast<AZ::u32>(AzToolsFramework::ViewportInteraction::KeyboardModifier::Ctrl);

        AzToolsFramework::ViewportInteraction::MouseInteractionResult handled =
            AzToolsFramework::ViewportInteraction::MouseInteractionResult::None;
        AzToolsFramework::EditorInteractionSystemViewportSelectionRequestBus::BroadcastResult(
            handled,
            &AzToolsFramework::ViewportInteraction::InternalMouseViewportRequests::InternalHandleAllMouseInteractions,
            interactionEvent);

        return handled;
    }
} // namespace LmbrCentral

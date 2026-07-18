#pragma once

import folia.platform.editor_interaction;

#include "editor/rendering/EditorRenderResources.h"

namespace winrt::Folia
{
    using folia::platform::editor::EditorInteractionMap;
    using folia::platform::editor::EditorTableAction;
    using folia::platform::editor::EditorTableActionKind;
    using folia::platform::editor::EditorVisualTable;

    struct EditorTableInteraction
    {
        static void Paint(EditorRenderResources& resources, EditorInteractionMap const& interactionMap, std::optional<D2D1_POINT_2F> pointerPosition, std::optional<EditorTableAction> const& draggedAction, std::optional<std::size_t> dropIndex);
    };
}

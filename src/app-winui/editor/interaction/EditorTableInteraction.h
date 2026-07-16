#pragma once

#include "editor/interaction/EditorInteractionMap.h"
#include "editor/rendering/EditorRenderResources.h"

namespace winrt::ElMd
{
    enum class EditorTableActionKind
    {
        InsertRow,
        InsertColumn,
        DeleteRow,
        DeleteColumn,
        DragRow,
        DragColumn,
    };

    struct EditorTableAction
    {
        EditorTableActionKind kind = EditorTableActionKind::InsertRow;
        elmd::TextPosition sourcePosition;
        std::size_t index = 0;

        bool operator==(EditorTableAction const&) const = default;
    };

    struct EditorTableInteraction
    {
        static void Paint(EditorRenderResources& resources, EditorInteractionMap const& interactionMap, std::optional<D2D1_POINT_2F> pointerPosition, std::optional<EditorTableAction> const& draggedAction, std::optional<std::size_t> dropIndex);
        static std::optional<EditorTableAction> ActionAt(EditorInteractionMap const& interactionMap, float x, float y);
        static std::optional<std::size_t> DropIndexAt(EditorInteractionMap const& interactionMap, std::optional<EditorTableAction> const& draggedAction, float x, float y, bool rows);
    };
}

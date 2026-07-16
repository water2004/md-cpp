#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"

namespace winrt::ElMd
{
    void EditorSurfaceRenderer::ScrollBy(float delta) { SetScrollOffset(scrollOffset + delta); }
    void EditorSurfaceRenderer::QueueScrollBy(float delta)
    {
        if (!std::isfinite(delta) || delta == 0.0f) return;
        auto pendingDistance = scrollTarget - scrollOffset;
        if (pendingDistance * delta < 0.0f) scrollTarget = scrollOffset;
        scrollTarget = (std::clamp)(scrollTarget + delta, 0.0f, MaximumScrollOffset());
    }

    bool EditorSurfaceRenderer::AdvanceScrollAnimation(float elapsedSeconds)
    {
        auto distance = scrollTarget - scrollOffset;
        auto elapsed = (std::max)(0.0f, elapsedSeconds);
        if (std::fabs(distance) < 0.1f)
        {
            scrollOffset = scrollTarget;
            return false;
        }
        // Keep wheel motion responsive at the start while preserving a visible
        // inertial tail. Capping the step prevents accumulated wheel input from
        // turning the exponential response into an abrupt high-speed jump.
        constexpr float responseHalfLifeSeconds = 0.120f;
        constexpr float maximumSpeedDipPerSecond = 900.0f;
        auto response = 1.0f - std::exp2(-elapsed / responseHalfLifeSeconds);
        auto step = distance * response;
        auto maximumStep = maximumSpeedDipPerSecond * elapsed;
        if (std::fabs(step) > maximumStep) step = std::copysign(maximumStep, step);
        scrollOffset += step;
        return true;
    }

    void EditorSurfaceRenderer::SetScrollOffset(float value)
    {
        scrollOffset = (std::clamp)(value, 0.0f, MaximumScrollOffset());
        scrollTarget = scrollOffset;
    }
    float EditorSurfaceRenderer::ScrollOffset() const { return scrollOffset; }
    float EditorSurfaceRenderer::MaximumScrollOffset() const { return (std::max)(0.0f, totalDocumentHeight - resources.surfaceHeightDip); }
    float EditorSurfaceRenderer::ViewportHeight() const { return resources.surfaceHeightDip; }
    HANDLE EditorSurfaceRenderer::FrameLatencyWaitableObject() const { return resources.frameLatencyWaitableObject; }
    void EditorSurfaceRenderer::UpdatePointer(float x, float y) { pointerPosition = D2D1::Point2F(x, y); }
    void EditorSurfaceRenderer::ClearPointer() { pointerPosition.reset(); }
    void EditorSurfaceRenderer::SetTableDrag(std::optional<TableAction> action, std::optional<std::size_t> dropIndex) { draggedTableAction = std::move(action); tableDropIndex = dropIndex; }
    std::optional<EditorSurfaceRenderer::TableAction> EditorSurfaceRenderer::TableActionAt(float x, float y) const { return EditorTableInteraction::ActionAt(interactionMap, x, y); }
    std::optional<std::size_t> EditorSurfaceRenderer::TableDropIndexAt(float x, float y, bool rows) const { return EditorTableInteraction::DropIndexAt(interactionMap, draggedTableAction, x, y, rows); }

    bool EditorSurfaceRenderer::ScrollToPosition(elmd::TextPosition position)
    {
        auto previous = scrollOffset;
        if (auto bounds = CaretBounds(position))
        {
            auto margin = styleSheet.verticalPadding;
            if (bounds->top < margin) scrollOffset = (std::max)(0.0f, scrollOffset - (margin - bounds->top));
            else if (bounds->bottom > resources.surfaceHeightDip - margin) scrollOffset = (std::min)(MaximumScrollOffset(), scrollOffset + bounds->bottom - (resources.surfaceHeightDip - margin));
            scrollTarget = scrollOffset;
            return scrollOffset != previous;
        }
        if (auto owner = documentOwnerY.find(position.container_id.v); owner != documentOwnerY.end())
        {
            scrollOffset = (std::clamp)(
                owner->second - styleSheet.verticalPadding,
                0.0f,
                MaximumScrollOffset());
            scrollTarget = scrollOffset;
            return scrollOffset != previous;
        }
        for (auto const& block : interactionMap.blocks)
        {
            auto mapped = std::any_of(block.displayToSource.begin(), block.displayToSource.end(), [&](auto value) { return value.container_id == position.container_id; });
            if (block.sourceSpan.container_id != position.container_id && !mapped) continue;
            scrollOffset = (std::clamp)(block.documentY - styleSheet.verticalPadding, 0.0f, MaximumScrollOffset());
            scrollTarget = scrollOffset;
            return scrollOffset != previous;
        }
        return false;
    }

    std::optional<elmd::TextPosition> EditorSurfaceRenderer::HitTest(float x, float y) const
    {
        for (auto const& rect : nonInteractiveRegions)
            if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom)
                return std::nullopt;
        return interactionMap.HitTest(x, y);
    }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::TaskCheckboxAt(float x, float y) const { return interactionMap.TaskCheckboxAt(x, y); }
    std::optional<EditorSurfaceRenderer::FootnoteHit> EditorSurfaceRenderer::FootnoteAt(float x, float y) const { return interactionMap.FootnoteAt(x, y); }
    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(elmd::TextPosition position) const { return interactionMap.CaretBounds(position, styleSheet.body.lineHeight); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::MoveCaretVertically(elmd::TextPosition position, bool down, float& goalX) const { return interactionMap.MoveCaretVertically(position, down, goalX, styleSheet.body.lineHeight); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::VisualLineStart(elmd::TextPosition position) const { return interactionMap.VisualLineStart(position); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::VisualLineEnd(elmd::TextPosition position) const { return interactionMap.VisualLineEnd(position); }

}

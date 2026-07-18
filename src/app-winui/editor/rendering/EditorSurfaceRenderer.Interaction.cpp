#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"

namespace winrt::Folia
{
    void EditorSurfaceRenderer::ScrollBy(float delta) { SetScrollOffset(scrollState.Offset() + delta); }
    void EditorSurfaceRenderer::QueueScrollBy(float delta)
    {
        scrollState.Queue(delta, MaximumScrollOffset());
    }

    bool EditorSurfaceRenderer::AdvanceScrollAnimation(float elapsedSeconds)
    {
        return scrollState.Advance(elapsedSeconds);
    }

    void EditorSurfaceRenderer::SetScrollOffset(float value)
    {
        scrollState.Set(value, MaximumScrollOffset());
    }
    float EditorSurfaceRenderer::ScrollOffset() const { return scrollState.Offset(); }
    float EditorSurfaceRenderer::MaximumScrollOffset() const { return (std::max)(0.0f, totalDocumentHeight - resources.surfaceHeightDip); }
    float EditorSurfaceRenderer::ViewportHeight() const { return resources.surfaceHeightDip; }
    HANDLE EditorSurfaceRenderer::FrameLatencyWaitableObject() const { return resources.frameLatencyWaitableObject; }
    void EditorSurfaceRenderer::UpdatePointer(float x, float y) { pointerPosition = D2D1::Point2F(x, y); }
    void EditorSurfaceRenderer::ClearPointer() { pointerPosition.reset(); }
    void EditorSurfaceRenderer::SetTableDrag(std::optional<EditorTableAction> action, std::optional<std::size_t> dropIndex) { draggedTableAction = std::move(action); tableDropIndex = dropIndex; }
    std::optional<EditorTableAction> EditorSurfaceRenderer::TableActionAt(float x, float y) const { return interactionMap.TableActionAt(x, y); }
    std::optional<std::size_t> EditorSurfaceRenderer::TableDropIndexAt(float x, float y, bool rows) const { return interactionMap.TableDropIndexAt(draggedTableAction, x, y, rows); }

    bool EditorSurfaceRenderer::ScrollToPosition(folia::TextPosition position)
    {
        auto previous = scrollState.Offset();
        if (auto bounds = CaretBounds(position))
        {
            auto margin = styleSheet.verticalPadding;
            auto next = previous;
            if (bounds->top < margin) next = (std::max)(0.0f, previous - (margin - bounds->top));
            else if (bounds->bottom > resources.surfaceHeightDip - margin) next = (std::min)(MaximumScrollOffset(), previous + bounds->bottom - (resources.surfaceHeightDip - margin));
            scrollState.Set(next, MaximumScrollOffset());
            return scrollState.Offset() != previous;
        }
        if (preparedDocument)
        {
            auto owner = preparedDocument->ownerBlockIndex.find(position.container_id.v);
            if (owner != preparedDocument->ownerBlockIndex.end()
                && owner->second < preparedDocument->geometry.Size())
            {
                auto placement = preparedDocument->geometry.At(owner->second);
                scrollState.Set(
                    placement.top - styleSheet.verticalPadding,
                    MaximumScrollOffset());
                return scrollState.Offset() != previous;
            }
        }
        for (auto const& block : interactionMap.blocks)
        {
            auto mapped = std::any_of(block.displayToSource.begin(), block.displayToSource.end(), [&](auto value) { return value.container_id == position.container_id; });
            if (block.sourceSpan.container_id != position.container_id && !mapped) continue;
            scrollState.Set(block.documentY - styleSheet.verticalPadding, MaximumScrollOffset());
            return scrollState.Offset() != previous;
        }
        return false;
    }

    std::optional<folia::TextPosition> EditorSurfaceRenderer::HitTest(float x, float y) const
    {
        for (auto const& rect : nonInteractiveRegions)
            if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom)
                return std::nullopt;
        return interactionMap.HitTest(x, y);
    }
    std::optional<folia::TextPosition> EditorSurfaceRenderer::TaskCheckboxAt(float x, float y) const { return interactionMap.TaskCheckboxAt(x, y); }
    std::optional<EditorVisualFootnoteHit> EditorSurfaceRenderer::FootnoteAt(float x, float y) const { return interactionMap.FootnoteAt(x, y); }
    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(folia::TextPosition position) const { return interactionMap.CaretBounds(position, styleSheet.body.lineHeight); }
    std::optional<folia::TextPosition> EditorSurfaceRenderer::MoveCaretVertically(folia::TextPosition position, bool down, float& goalX) const { return interactionMap.MoveCaretVertically(position, down, goalX, styleSheet.body.lineHeight); }
    std::optional<folia::TextPosition> EditorSurfaceRenderer::VisualLineStart(folia::TextPosition position) const { return interactionMap.VisualLineStart(position); }
    std::optional<folia::TextPosition> EditorSurfaceRenderer::VisualLineEnd(folia::TextPosition position) const { return interactionMap.VisualLineEnd(position); }

}

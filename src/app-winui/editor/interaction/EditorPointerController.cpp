#include "pch.h"
#include "editor/interaction/EditorPointerController.h"

import elmd.core.command;

namespace winrt::ElMd
{
    void EditorPointerController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorTextInputController& textInput,
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& surface,
        ExecuteCommand executeCommand,
        Render render,
        OpenLink openLink,
        OpenFootnote openFootnote,
        ResetCaretGoal resetCaretGoal)
    {
        Detach();
        session_ = &session;
        renderer_ = &renderer;
        textInput_ = &textInput;
        surface_ = surface;
        executeCommand_ = std::move(executeCommand);
        render_ = std::move(render);
        openLink_ = std::move(openLink);
        openFootnote_ = std::move(openFootnote);
        resetCaretGoal_ = std::move(resetCaretGoal);
    }

    void EditorPointerController::Detach()
    {
        if (renderer_)
        {
            renderer_->SetTableDrag(std::nullopt, std::nullopt);
            renderer_->ClearPointer();
        }
        selecting_ = false;
        hoverTooltip_.reset();
        tableDrag_.reset();
        tableDropIndex_.reset();
        executeCommand_ = {};
        render_ = {};
        openLink_ = {};
        openFootnote_ = {};
        resetCaretGoal_ = {};
        surface_ = nullptr;
        textInput_ = nullptr;
        renderer_ = nullptr;
        session_ = nullptr;
    }

    void EditorPointerController::PointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!session_ || !renderer_ || !surface_) return;
        surface_.Focus(winrt::Microsoft::UI::Xaml::FocusState::Pointer);
        if (resetCaretGoal_) resetCaretGoal_();
        auto point = args.GetCurrentPoint(surface_).Position();
        renderer_->UpdatePointer(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (auto action = renderer_->TableActionAt(static_cast<float>(point.X), static_cast<float>(point.Y)))
        {
            if (action->kind == EditorSurfaceRenderer::TableActionKind::DragRow || action->kind == EditorSurfaceRenderer::TableActionKind::DragColumn)
            {
                tableDrag_ = action;
                auto rows = action->kind == EditorSurfaceRenderer::TableActionKind::DragRow;
                tableDropIndex_ = renderer_->TableDropIndexAt(static_cast<float>(point.X), static_cast<float>(point.Y), rows);
                renderer_->SetTableDrag(tableDrag_, tableDropIndex_);
                surface_.CapturePointer(args.Pointer());
                if (render_) render_();
                args.Handled(true);
                return;
            }

            session_->SetSelection(action->sourcePosition, action->sourcePosition);
            if (textInput_) textInput_->NotifySelectionChanged();
            elmd::Command command;
            command.table_index = action->index;
            switch (action->kind)
            {
                case EditorSurfaceRenderer::TableActionKind::InsertRow:
                    command.kind = elmd::CommandKind::InsertTableRowAt;
                    break;
                case EditorSurfaceRenderer::TableActionKind::InsertColumn:
                    command.kind = elmd::CommandKind::InsertTableColumnAt;
                    break;
                case EditorSurfaceRenderer::TableActionKind::DeleteRow:
                    command.kind = elmd::CommandKind::DeleteTableRow;
                    break;
                case EditorSurfaceRenderer::TableActionKind::DeleteColumn:
                    command.kind = elmd::CommandKind::DeleteTableColumn;
                    break;
                default:
                    return;
            }
            if (executeCommand_) executeCommand_(command);
            args.Handled(true);
            return;
        }

        if (auto checkbox = renderer_->TaskCheckboxAt(static_cast<float>(point.X), static_cast<float>(point.Y)))
        {
            session_->SetSelection(*checkbox, *checkbox);
            if (textInput_) textInput_->NotifySelectionChanged();
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleTaskCheckbox;
            if (executeCommand_) executeCommand_(command);
            args.Handled(true);
            return;
        }
        if (auto footnote = renderer_->FootnoteAt(static_cast<float>(point.X), static_cast<float>(point.Y)))
        {
            if (openFootnote_) openFootnote_(std::move(*footnote), point);
            args.Handled(true);
            return;
        }
        auto hit = renderer_->HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (!hit) return;
        if ((args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Control) == winrt::Windows::System::VirtualKeyModifiers::Control)
        {
            if (auto link = LinkAtPosition(*hit))
            {
                if (openLink_) openLink_(std::move(*link));
                args.Handled(true);
                return;
            }
        }
        selecting_ = true;
        anchor_ = *hit;
        session_->SetSelection(anchor_, anchor_);
        if (textInput_) textInput_->NotifySelectionChanged();
        surface_.CapturePointer(args.Pointer());
        if (render_) render_();
        args.Handled(true);
    }

    void EditorPointerController::PointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!session_ || !renderer_ || !surface_) return;
        auto point = args.GetCurrentPoint(surface_).Position();
        renderer_->UpdatePointer(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (!selecting_ && !tableDrag_)
        {
            auto hit = renderer_->HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
            auto tooltip = hit ? TooltipAtPosition(*hit) : std::nullopt;
            if (tooltip != hoverTooltip_)
            {
                hoverTooltip_ = tooltip;
                winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                    surface_,
                    tooltip ? winrt::box_value(winrt::to_hstring(*tooltip)) : nullptr);
            }
        }
        if (tableDrag_)
        {
            auto rows = tableDrag_->kind == EditorSurfaceRenderer::TableActionKind::DragRow;
            tableDropIndex_ = renderer_->TableDropIndexAt(static_cast<float>(point.X), static_cast<float>(point.Y), rows);
            renderer_->SetTableDrag(tableDrag_, tableDropIndex_);
            if (render_) render_();
            args.Handled(true);
            return;
        }
        if (!selecting_)
        {
            if (render_) render_();
            return;
        }
        auto hit = renderer_->HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (!hit) return;
        session_->SetSelection(anchor_, *hit);
        if (textInput_) textInput_->NotifySelectionChanged();
        if (render_) render_();
        args.Handled(true);
    }

    void EditorPointerController::PointerReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!session_ || !renderer_ || !surface_) return;
        if (tableDrag_)
        {
            auto action = *tableDrag_;
            auto dropIndex = tableDropIndex_;
            tableDrag_.reset();
            tableDropIndex_.reset();
            renderer_->SetTableDrag(std::nullopt, std::nullopt);
            surface_.ReleasePointerCapture(args.Pointer());
            if (dropIndex)
            {
                session_->SetSelection(action.sourcePosition, action.sourcePosition);
                if (textInput_) textInput_->NotifySelectionChanged();
                elmd::Command command;
                command.kind = action.kind == EditorSurfaceRenderer::TableActionKind::DragRow
                    ? elmd::CommandKind::MoveTableRowTo
                    : elmd::CommandKind::MoveTableColumnTo;
                command.table_index = *dropIndex;
                if (executeCommand_) executeCommand_(command);
            }
            else if (render_)
            {
                render_();
            }
            args.Handled(true);
            return;
        }
        if (selecting_)
        {
            selecting_ = false;
            surface_.ReleasePointerCapture(args.Pointer());
            args.Handled(true);
        }
    }

    void EditorPointerController::PointerExited()
    {
        hoverTooltip_.reset();
        if (surface_) winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(surface_, nullptr);
        if (!tableDrag_ && renderer_)
        {
            renderer_->ClearPointer();
            if (render_) render_();
        }
    }

    void EditorPointerController::DoubleTapped(winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
    {
        if (!renderer_ || !surface_) return;
        surface_.Focus(winrt::Microsoft::UI::Xaml::FocusState::Pointer);
        auto point = args.GetPosition(surface_);
        auto hit = renderer_->HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (hit && SelectWordAt(*hit))
        {
            if (render_) render_();
            args.Handled(true);
        }
    }

    bool EditorPointerController::SelectWordAt(elmd::TextPosition position)
    {
        if (!session_) return false;
        auto source = session_->EditableSource(position.container_id);
        if (!source || source->empty()) return false;
        auto const& text = *source;
        auto offset = position.source_offset;
        auto isWordChar = [](char32_t ch)
        {
            return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z') || (ch >= U'0' && ch <= U'9') || ch == U'_' || ch > 0x7f;
        };
        if (offset >= text.size()) offset = text.size() - 1;
        if (!isWordChar(text[offset]) && offset > 0 && isWordChar(text[offset - 1])) --offset;
        if (!isWordChar(text[offset])) return false;
        auto start = offset;
        while (start > 0 && isWordChar(text[start - 1])) --start;
        auto end = offset + 1;
        while (end < text.size() && isWordChar(text[end])) ++end;
        session_->SetSelection(
            {position.container_id, start, elmd::TextAffinity::Downstream},
            {position.container_id, end, elmd::TextAffinity::Downstream});
        if (textInput_) textInput_->NotifySelectionChanged();
        return true;
    }

    std::optional<std::string> EditorPointerController::LinkAtPosition(elmd::TextPosition position) const
    {
        if (!session_) return std::nullopt;
        auto scanItems = [&](auto& self, auto const& items) -> std::optional<std::string>
        {
            for (auto const& item : items)
            {
                if (item.kind == elmd::InlineRenderItem::Kind::Link && item.source_span.container_id == position.container_id && item.source_span.source_range.covers(position.source_offset)) return item.href;
                if (!item.children.empty()) if (auto nested = self(self, item.children)) return nested;
            }
            return std::nullopt;
        };
        auto scanBlock = [&](auto& self, auto const& block) -> std::optional<std::string>
        {
            if (block.kind == elmd::RenderBlockKind::Image && block.link && block.source_span.container_id == position.container_id) return *block.link;
            if (auto link = scanItems(scanItems, block.inline_items)) return link;
            for (auto const& cell : block.table_cells) if (auto link = scanItems(scanItems, cell)) return link;
            for (auto const& child : block.child_blocks) if (auto link = self(self, child)) return link;
            return std::nullopt;
        };
        for (auto const& block : session_->RenderModel().blocks) if (auto link = scanBlock(scanBlock, block)) return link;
        return std::nullopt;
    }

    std::optional<std::string> EditorPointerController::TooltipAtPosition(elmd::TextPosition position) const
    {
        if (!session_) return std::nullopt;
        auto scanItems = [&](auto& self, auto const& items) -> std::optional<std::string>
        {
            for (auto const& item : items)
            {
                if (item.source_span.container_id == position.container_id && item.source_span.source_range.covers(position.source_offset))
                {
                    if (item.title && !item.title->empty()) return *item.title;
                    if (item.kind == elmd::InlineRenderItem::Kind::Link && !item.href.empty()) return item.href;
                    if (item.kind == elmd::InlineRenderItem::Kind::Image && !item.alt.empty()) return item.alt;
                }
                if (!item.children.empty()) if (auto nested = self(self, item.children)) return nested;
            }
            return std::nullopt;
        };
        auto scanBlock = [&](auto& self, auto const& block) -> std::optional<std::string>
        {
            if (block.kind == elmd::RenderBlockKind::Image && block.source_span.container_id == position.container_id)
            {
                if (block.title && !block.title->empty()) return *block.title;
                if (!block.alt.empty()) return block.alt;
            }
            if (auto tooltip = scanItems(scanItems, block.inline_items)) return tooltip;
            for (auto const& cell : block.table_cells) if (auto tooltip = scanItems(scanItems, cell)) return tooltip;
            for (auto const& child : block.child_blocks) if (auto tooltip = self(self, child)) return tooltip;
            return std::nullopt;
        };
        for (auto const& block : session_->RenderModel().blocks) if (auto tooltip = scanBlock(scanBlock, block)) return tooltip;
        return std::nullopt;
    }
}

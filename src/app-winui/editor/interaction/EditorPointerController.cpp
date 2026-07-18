#include "pch.h"
#include "editor/interaction/EditorPointerController.h"

import elmd.core.command;

namespace winrt::ElMd
{
    void EditorPointerController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorScrollController& scroll,
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
        scroll_ = &scroll;
        textInput_ = &textInput;
        surface_ = surface;
        selectionDrag_.Attach(session, renderer, scroll, textInput, surface);
        textCursor_ = winrt::Microsoft::UI::Input::InputSystemCursor::Create(
            winrt::Microsoft::UI::Input::InputSystemCursorShape::IBeam);
        linkCursor_ = winrt::Microsoft::UI::Input::InputSystemCursor::Create(
            winrt::Microsoft::UI::Input::InputSystemCursorShape::Hand);
        linkCursorActive_ = true;
        SetLinkCursor(false);
        executeCommand_ = std::move(executeCommand);
        render_ = std::move(render);
        openLink_ = std::move(openLink);
        openFootnote_ = std::move(openFootnote);
        resetCaretGoal_ = std::move(resetCaretGoal);
    }

    void EditorPointerController::Detach()
    {
        selectionDrag_.Detach();
        if (surface_)
        {
            if (auto protectedElement = surface_.try_as<winrt::Microsoft::UI::Xaml::IUIElementProtected>())
                protectedElement.ProtectedCursor(nullptr);
        }
        if (renderer_)
        {
            renderer_->SetTableDrag(std::nullopt, std::nullopt);
            renderer_->ClearPointer();
        }
        hoverTooltip_.reset();
        hoverTaskCheckbox_.reset();
        hoverTableAction_.reset();
        tableDrag_.reset();
        tableDropIndex_.reset();
        executeCommand_ = {};
        render_ = {};
        openLink_ = {};
        openFootnote_ = {};
        resetCaretGoal_ = {};
        linkCursorActive_ = false;
        linkCursor_ = nullptr;
        textCursor_ = nullptr;
        surface_ = nullptr;
        textInput_ = nullptr;
        scroll_ = nullptr;
        renderer_ = nullptr;
        session_ = nullptr;
    }

    void EditorPointerController::PointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!session_ || !renderer_ || !surface_) return;
        surface_.Focus(winrt::Microsoft::UI::Xaml::FocusState::Pointer);
        if (resetCaretGoal_) resetCaretGoal_();
        auto point = args.GetCurrentPoint(surface_).Position();
        if (scroll_) scroll_->Stop();
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
        selectionDrag_.Begin(*hit);
        surface_.CapturePointer(args.Pointer());
        if (render_) render_();
        args.Handled(true);
    }

    void EditorPointerController::PointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!session_ || !renderer_ || !surface_) return;
        auto point = args.GetCurrentPoint(surface_).Position();
        auto taskCheckbox = renderer_->TaskCheckboxAt(static_cast<float>(point.X), static_cast<float>(point.Y));
        auto tableAction = renderer_->TableActionAt(static_cast<float>(point.X), static_cast<float>(point.Y));
        auto hoverVisualChanged = taskCheckbox != hoverTaskCheckbox_ || tableAction != hoverTableAction_;
        hoverTaskCheckbox_ = std::move(taskCheckbox);
        hoverTableAction_ = std::move(tableAction);
        renderer_->UpdatePointer(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (!selectionDrag_.Active() && !tableDrag_)
        {
            auto hit = renderer_->HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
            auto interaction = hit && session_ ? session_->InteractionAt(*hit) : std::nullopt;
            auto tooltip = interaction ? std::move(interaction->tooltip) : std::nullopt;
            SetLinkCursor(interaction && interaction->link.has_value());
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
            SetLinkCursor(false);
            auto rows = tableDrag_->kind == EditorSurfaceRenderer::TableActionKind::DragRow;
            tableDropIndex_ = renderer_->TableDropIndexAt(static_cast<float>(point.X), static_cast<float>(point.Y), rows);
            renderer_->SetTableDrag(tableDrag_, tableDropIndex_);
            if (render_) render_();
            args.Handled(true);
            return;
        }
        if (!selectionDrag_.Active())
        {
            if (hoverVisualChanged && render_) render_();
            return;
        }
        SetLinkCursor(false);
        selectionDrag_.Update(static_cast<float>(point.X), static_cast<float>(point.Y));
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
        if (selectionDrag_.Active())
        {
            auto point = args.GetCurrentPoint(surface_).Position();
            selectionDrag_.Finish(static_cast<float>(point.X), static_cast<float>(point.Y));
            surface_.ReleasePointerCapture(args.Pointer());
            args.Handled(true);
        }
    }

    void EditorPointerController::PointerExited()
    {
        SetLinkCursor(false);
        hoverTooltip_.reset();
        hoverTaskCheckbox_.reset();
        hoverTableAction_.reset();
        if (surface_) winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(surface_, nullptr);
        if (!tableDrag_ && renderer_)
        {
            renderer_->ClearPointer();
            if (render_) render_();
        }
    }

    void EditorPointerController::CancelPointerInteraction()
    {
        selectionDrag_.Cancel();
        SetLinkCursor(false);
        if (tableDrag_ && renderer_)
        {
            tableDrag_.reset();
            tableDropIndex_.reset();
            renderer_->SetTableDrag(std::nullopt, std::nullopt);
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

    void EditorPointerController::SetLinkCursor(bool link)
    {
        if (!surface_ || link == linkCursorActive_) return;
        auto protectedElement = surface_.try_as<winrt::Microsoft::UI::Xaml::IUIElementProtected>();
        if (!protectedElement) return;
        protectedElement.ProtectedCursor(link ? linkCursor_ : textCursor_);
        linkCursorActive_ = link;
    }

    std::optional<std::string> EditorPointerController::LinkAtPosition(elmd::TextPosition position) const
    {
        if (!session_) return std::nullopt;
        auto interaction = session_->InteractionAt(position);
        return interaction ? std::move(interaction->link) : std::nullopt;
    }

}

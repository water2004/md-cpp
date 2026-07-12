#include "pch.h"
#include "EditorTextInputController.h"
#include "EditorContentPreparation.h"

import elmd.core.command;
import elmd.core.utf;

namespace winrt::ElMd
{
    EditorTextInputController::~EditorTextInputController()
    {
        Detach();
    }

    void EditorTextInputController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& surface,
        ExecuteCommand executeCommand,
        Render render,
        WindowHandle windowHandle)
    {
        Detach();
        session_ = &session;
        renderer_ = &renderer;
        surface_ = surface;
        executeCommand_ = std::move(executeCommand);
        render_ = std::move(render);
        windowHandle_ = std::move(windowHandle);
        auto manager = winrt::Windows::UI::Text::Core::CoreTextServicesManager::GetForCurrentView();
        context_ = manager.CreateEditContext();
        context_.InputScope(winrt::Windows::UI::Text::Core::CoreTextInputScope::Text);
        context_.InputPaneDisplayPolicy(winrt::Windows::UI::Text::Core::CoreTextInputPaneDisplayPolicy::Automatic);
        knownLength_ = session_->AcpLength();
        RegisterHandlers();
    }

    void EditorTextInputController::Detach()
    {
        if (focused_ && context_)
        {
            context_.NotifyFocusLeave();
        }
        focused_ = false;
        RevokeHandlers();
        context_ = nullptr;
        surface_ = nullptr;
        executeCommand_ = {};
        render_ = {};
        windowHandle_ = {};
        session_ = nullptr;
        renderer_ = nullptr;
        updating_ = false;
        ClearPendingCharacterTextUpdate();
    }

    void EditorTextInputController::FocusEnter()
    {
        if (!context_ || focused_) return;
        focused_ = true;
        context_.NotifyFocusEnter();
        context_.NotifySelectionChanged(CurrentSelection());
    }

    void EditorTextInputController::FocusLeave()
    {
        if (!context_ || !focused_) return;
        focused_ = false;
        context_.NotifyFocusLeave();
    }

    void EditorTextInputController::NotifyTextChanged(std::size_t oldLength)
    {
        if (!context_ || updating_ || !session_) return;
        auto newLength = session_->AcpLength();
        context_.NotifyTextChanged(
            winrt::Windows::UI::Text::Core::CoreTextRange{ 0, static_cast<int32_t>(oldLength) },
            static_cast<int32_t>(newLength),
            CurrentSelection());
        context_.NotifySelectionChanged(CurrentSelection());
        knownLength_ = newLength;
    }

    void EditorTextInputController::NotifySelectionChanged()
    {
        if (context_) context_.NotifySelectionChanged(CurrentSelection());
    }

    void EditorTextInputController::RecordCharacterTextUpdate(std::size_t start, std::u32string text)
    {
        pendingCharacterUpdate_ = true;
        pendingCharacterStart_ = start;
        pendingCharacterText_ = std::move(text);
    }

    void EditorTextInputController::ClearPendingCharacterTextUpdate()
    {
        pendingCharacterUpdate_ = false;
        pendingCharacterStart_ = 0;
        pendingCharacterText_.clear();
    }

    winrt::Windows::UI::Text::Core::CoreTextRange EditorTextInputController::CurrentSelection() const
    {
        if (!session_) return {};
        auto selection = session_->Selection();
        return { static_cast<int32_t>(session_->AcpOffset(selection.anchor)), static_cast<int32_t>(session_->AcpOffset(selection.active)) };
    }

    void EditorTextInputController::RegisterHandlers()
    {
        textRequestedToken_ = context_.TextRequested([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextTextRequestedEventArgs const& args)
        {
            if (!session_) return;
            auto request = args.Request();
            auto range = request.Range();
            auto boundary = session_->BoundaryTextUtf16();
            auto text = winrt::hstring(boundary);
            auto textLength = static_cast<int32_t>(text.size());
            auto start = (std::max)(0, (std::min)(range.StartCaretPosition, textLength));
            auto end = (std::max)(start, (std::min)(range.EndCaretPosition, textLength));
            request.Text(winrt::hstring(text.c_str() + start, static_cast<uint32_t>(end - start)));
        });

        selectionRequestedToken_ = context_.SelectionRequested([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextSelectionRequestedEventArgs const& args)
        {
            args.Request().Selection(CurrentSelection());
        });

        layoutRequestedToken_ = context_.LayoutRequested([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextLayoutRequestedEventArgs const& args)
        {
            if (!session_ || !renderer_ || !surface_ || !windowHandle_) return;
            try
            {
                auto request = args.Request();
                auto selectionState = session_->Selection();
                auto caretBounds = renderer_->CaretBounds(selectionState.active, selectionState.active.affinity == elmd::TextAffinity::Upstream);
                auto textRect = winrt::Windows::Foundation::Rect{ 0.0f, 0.0f, 1.0f, 24.0f };
                auto controlRect = winrt::Windows::Foundation::Rect{ 0.0f, 0.0f, static_cast<float>(surface_.ActualWidth()), static_cast<float>(surface_.ActualHeight()) };
                auto transform = surface_.TransformToVisual(nullptr);
                auto hwnd = windowHandle_();
                POINT clientOrigin{};
                ClientToScreen(hwnd, &clientOrigin);
                auto dpi = static_cast<float>(GetDpiForWindow(hwnd));
                auto scale = dpi > 0.0f ? dpi / 96.0f : 1.0f;
                auto screenOrigin = winrt::Windows::Foundation::Point{ static_cast<float>(clientOrigin.x), static_cast<float>(clientOrigin.y) };
                if (caretBounds)
                {
                    auto point = transform.TransformPoint({ caretBounds->left, caretBounds->top });
                    textRect = { screenOrigin.X + point.X * scale, screenOrigin.Y + point.Y * scale, (caretBounds->right - caretBounds->left) * scale, (caretBounds->bottom - caretBounds->top) * scale };
                }
                auto controlTopLeft = transform.TransformPoint({ 0.0f, 0.0f });
                controlRect.X = screenOrigin.X + controlTopLeft.X * scale;
                controlRect.Y = screenOrigin.Y + controlTopLeft.Y * scale;
                controlRect.Width *= scale;
                controlRect.Height *= scale;
                auto bounds = request.LayoutBounds();
                bounds.TextBounds(textRect);
                bounds.ControlBounds(controlRect);
            }
            catch (winrt::hresult_error const&)
            {
            }
        });

        selectionUpdatingToken_ = context_.SelectionUpdating([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextSelectionUpdatingEventArgs const& args)
        {
            if (!session_) return;
            auto selection = args.Selection();
            auto length = session_->AcpLength();
            auto start = static_cast<std::size_t>((std::max)(0, selection.StartCaretPosition));
            auto end = static_cast<std::size_t>((std::max)(0, selection.EndCaretPosition));
            session_->SetSelection(
                session_->PositionFromAcp((std::min)(start, length)),
                session_->PositionFromAcp((std::min)(end, length)));
            if (render_) render_();
            args.Result(winrt::Windows::UI::Text::Core::CoreTextSelectionUpdatingResult::Succeeded);
        });

        textUpdatingToken_ = context_.TextUpdating([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextTextUpdatingEventArgs const& args)
        {
            if (!session_ || !executeCommand_)
            {
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
                return;
            }
            auto range = args.Range();
            auto length = session_->AcpLength();
            auto start = static_cast<std::size_t>((std::max)(0, range.StartCaretPosition));
            auto end = static_cast<std::size_t>((std::max)(0, range.EndCaretPosition));
            auto incoming = elmd::utf8_to_cps(winrt::to_string(args.Text()));
            auto isIncomingNewline = incoming == U"\r" || incoming == U"\n" || incoming == U"\r\n";
            auto selection = session_->Selection();
            auto text = session_->BoundaryTextUtf16();
            auto activeAcp = session_->AcpOffset(selection.active);
            if (isIncomingNewline && start < text.size() && text[start] == L'\n' && selection.is_caret() && activeAcp == start + 1)
            {
                if (render_) render_();
                context_.NotifySelectionChanged(CurrentSelection());
                knownLength_ = length;
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
                return;
            }
            if (pendingCharacterUpdate_
                && start <= text.size()
                && start == pendingCharacterStart_
                && incoming == pendingCharacterText_
                && start + ToWide(pendingCharacterText_).size() <= text.size()
                && selection.is_caret()
                && activeAcp == start + ToWide(pendingCharacterText_).size()
                && text.substr(start, ToWide(pendingCharacterText_).size()) == ToWide(pendingCharacterText_))
            {
                ClearPendingCharacterTextUpdate();
                auto newSelection = args.NewSelection();
                auto newStart = static_cast<std::size_t>((std::max)(0, newSelection.StartCaretPosition));
                auto newEnd = static_cast<std::size_t>((std::max)(0, newSelection.EndCaretPosition));
                session_->SetSelection(
                    session_->PositionFromAcp((std::min)(newStart, length)),
                    session_->PositionFromAcp((std::min)(newEnd, length)));
                if (render_) render_();
                context_.NotifySelectionChanged(CurrentSelection());
                knownLength_ = length;
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
                return;
            }
            ClearPendingCharacterTextUpdate();
            session_->SetSelection(
                session_->PositionFromAcp((std::min)(start, length)),
                session_->PositionFromAcp((std::min)(end, length)));
            auto command = isIncomingNewline ? elmd::Command{} : elmd::Command::InsertText(incoming);
            if (isIncomingNewline) command.kind = elmd::CommandKind::InsertNewline;
            updating_ = true;
            auto executed = executeCommand_(command);
            updating_ = false;
            if (!executed)
            {
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
                return;
            }
            auto newLength = session_->AcpLength();
            if (!isIncomingNewline)
            {
                auto newSelection = args.NewSelection();
                auto newStart = static_cast<std::size_t>((std::max)(0, newSelection.StartCaretPosition));
                auto newEnd = static_cast<std::size_t>((std::max)(0, newSelection.EndCaretPosition));
                session_->SetSelection(
                    session_->PositionFromAcp((std::min)(newStart, newLength)),
                    session_->PositionFromAcp((std::min)(newEnd, newLength)));
            }
            if (render_) render_();
            context_.NotifySelectionChanged(CurrentSelection());
            knownLength_ = newLength;
            args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
        });
    }

    void EditorTextInputController::RevokeHandlers()
    {
        if (!context_) return;
        if (textRequestedToken_.value) context_.TextRequested(textRequestedToken_);
        if (selectionRequestedToken_.value) context_.SelectionRequested(selectionRequestedToken_);
        if (layoutRequestedToken_.value) context_.LayoutRequested(layoutRequestedToken_);
        if (selectionUpdatingToken_.value) context_.SelectionUpdating(selectionUpdatingToken_);
        if (textUpdatingToken_.value) context_.TextUpdating(textUpdatingToken_);
        textRequestedToken_ = {};
        selectionRequestedToken_ = {};
        layoutRequestedToken_ = {};
        selectionUpdatingToken_ = {};
        textUpdatingToken_ = {};
    }
}

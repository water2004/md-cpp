#include "pch.h"
#include "editor/interaction/EditorTextInputController.h"
#include "editor/rendering/EditorContentPreparation.h"

import elmd.core.command;
import elmd.core.utf;

namespace
{
    using CoreTextRange = winrt::Windows::UI::Text::Core::CoreTextRange;

    struct TextStoreChange
    {
        CoreTextRange modifiedRange{};
        std::int32_t replacementLength = 0;
    };

    std::int32_t SafeAcp(std::size_t value)
    {
        return static_cast<std::int32_t>((std::min)(
            value,
            static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())));
    }

    bool IsHighSurrogate(wchar_t value)
    {
        return value >= 0xd800 && value <= 0xdbff;
    }

    bool IsLowSurrogate(wchar_t value)
    {
        return value >= 0xdc00 && value <= 0xdfff;
    }

    std::optional<TextStoreChange> Difference(
        std::wstring_view previous,
        std::wstring_view current)
    {
        if (previous == current) return std::nullopt;

        std::size_t prefix = 0;
        const auto common = (std::min)(previous.size(), current.size());
        while (prefix < common && previous[prefix] == current[prefix]) ++prefix;
        if (prefix > 0 && prefix < previous.size() && prefix < current.size()
            && (IsHighSurrogate(previous[prefix - 1]) || IsHighSurrogate(current[prefix - 1])))
        {
            --prefix;
        }

        std::size_t suffix = 0;
        while (suffix < previous.size() - prefix
            && suffix < current.size() - prefix
            && previous[previous.size() - suffix - 1] == current[current.size() - suffix - 1])
        {
            ++suffix;
        }
        if (suffix > 0)
        {
            const auto previousStart = previous.size() - suffix;
            const auto currentStart = current.size() - suffix;
            if ((previousStart > 0 && previousStart < previous.size()
                    && IsHighSurrogate(previous[previousStart - 1])
                    && IsLowSurrogate(previous[previousStart]))
                || (currentStart > 0 && currentStart < current.size()
                    && IsHighSurrogate(current[currentStart - 1])
                    && IsLowSurrogate(current[currentStart])))
            {
                --suffix;
            }
        }

        return TextStoreChange{
            {SafeAcp(prefix), SafeAcp(previous.size() - suffix)},
            SafeAcp(current.size() - prefix - suffix),
        };
    }
}

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
        knownText_ = session_->BoundaryTextUtf16();
        lifetime_ = std::make_shared<int>(0);
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
        notifying_ = false;
        synchronizationQueued_ = false;
        knownText_.clear();
        lifetime_.reset();
    }

    void EditorTextInputController::FocusEnter()
    {
        if (!context_ || focused_) return;
        focused_ = true;
        context_.NotifyFocusEnter();
        NotifySelectionChanged();
    }

    void EditorTextInputController::FocusLeave()
    {
        if (!context_ || !focused_) return;
        focused_ = false;
        context_.NotifyFocusLeave();
    }

    void EditorTextInputController::NotifyTextChanged()
    {
        if (!context_ || !session_) return;
        if (updating_ || notifying_ || synchronizationQueued_)
        {
            QueueSynchronization();
            return;
        }
        SynchronizeTextStore();
    }

    void EditorTextInputController::NotifySelectionChanged()
    {
        if (!context_ || !session_) return;
        if (updating_ || notifying_ || synchronizationQueued_)
        {
            QueueSynchronization();
            return;
        }
        try
        {
            context_.NotifySelectionChanged(CurrentSelection());
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    void EditorTextInputController::QueueSynchronization()
    {
        if (synchronizationQueued_ || !surface_ || !lifetime_) return;
        auto dispatcher = surface_.DispatcherQueue();
        if (!dispatcher) return;
        synchronizationQueued_ = true;
        auto lifetime = std::weak_ptr<int>{lifetime_};
        if (!dispatcher.TryEnqueue([this, lifetime]
        {
            if (!lifetime.lock()) return;
            synchronizationQueued_ = false;
            SynchronizeTextStore();
        }))
        {
            synchronizationQueued_ = false;
        }
    }

    void EditorTextInputController::SynchronizeTextStore()
    {
        if (!context_ || !session_) return;
        if (updating_ || notifying_)
        {
            QueueSynchronization();
            return;
        }

        auto current = session_->BoundaryTextUtf16();
        auto change = Difference(knownText_, current);
        if (!change)
        {
            NotifySelectionChanged();
            return;
        }

        auto previous = knownText_;
        knownText_ = std::move(current);
        notifying_ = true;
        try
        {
            // newLength is the UTF-16 length of the replacement for the old
            // modified range, not the length of the complete document. The
            // selection is carried by this notification; sending a second
            // selection notification here would re-enter TextInputFramework.
            context_.NotifyTextChanged(
                change->modifiedRange,
                change->replacementLength,
                CurrentSelection());
        }
        catch (winrt::hresult_error const&)
        {
            // Keep the local mirror at the state CoreText last accepted. A
            // later document operation can retry synchronization safely.
            knownText_ = std::move(previous);
        }
        notifying_ = false;
    }

    winrt::Windows::UI::Text::Core::CoreTextRange EditorTextInputController::CurrentSelection() const
    {
        if (!session_) return {};
        auto selection = session_->Selection();
        const auto length = session_->AcpLength();
        const auto anchor = (std::min)(session_->AcpOffset(selection.anchor), length);
        const auto active = (std::min)(session_->AcpOffset(selection.active), length);
        return {SafeAcp(anchor), SafeAcp(active)};
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
                auto caretBounds = renderer_->CaretBounds(selectionState.active);
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
            if (!session_ || !executeCommand_ || args.IsCanceled())
            {
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
                return;
            }
            auto range = args.Range();
            auto length = session_->AcpLength();
            auto start = static_cast<std::size_t>((std::max)(0, range.StartCaretPosition));
            auto end = static_cast<std::size_t>((std::max)(0, range.EndCaretPosition));
            auto incomingHstring = args.Text();
            auto incoming = elmd::utf8_to_cps(winrt::to_string(incomingHstring));
            auto isIncomingNewline = incoming == U"\r" || incoming == U"\n" || incoming == U"\r\n";
            auto selection = session_->Selection();
            auto text = session_->BoundaryTextUtf16();
            auto activeAcp = session_->AcpOffset(selection.active);
            if (isIncomingNewline && start < text.size() && text[start] == L'\n' && selection.is_caret() && activeAcp == start + 1)
            {
                // Enter is handled as a semantic structure command by the key
                // controller. Reject the duplicate CoreText update: accepting
                // it would make the text service advance a second newline.
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
                QueueSynchronization();
                return;
            }

            const auto storeStart = (std::min)(start, knownText_.size());
            const auto storeEnd = (std::max)(storeStart, (std::min)(end, knownText_.size()));
            auto expectedText = knownText_;
            auto incomingText = std::wstring_view{incomingHstring.c_str(), incomingHstring.size()};
            expectedText.replace(storeStart, storeEnd - storeStart, incomingText);
            auto previousKnownText = knownText_;
            // Once this callback succeeds, CoreText will know the requested
            // replacement. Semantic marker conversions can produce a
            // different editor projection; QueueSynchronization reports that
            // extra delta only after this callback has returned.
            knownText_ = std::move(expectedText);

            session_->SetSelection(
                session_->PositionFromAcp((std::min)(start, length)),
                session_->PositionFromAcp((std::min)(end, length)));
            auto command = elmd::Command::InsertText(incoming);
            if (isIncomingNewline) command.kind = elmd::CommandKind::InsertNewline;
            else if (incoming.empty()) command.kind = elmd::CommandKind::DeleteSelection;
            updating_ = true;
            bool executed = false;
            try
            {
                executed = executeCommand_(command);
            }
            catch (...)
            {
                executed = false;
            }
            updating_ = false;
            if (!executed)
            {
                knownText_ = std::move(previousKnownText);
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
                QueueSynchronization();
                return;
            }
            args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
            QueueSynchronization();
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

#include "pch.h"
#include "editor/interaction/EditorTextInputController.h"
#include "editor/rendering/EditorContentPreparation.h"

import elmd.core.command;
import elmd.core.utf;

namespace
{
    using CoreTextRange = winrt::Windows::UI::Text::Core::CoreTextRange;

    std::int32_t SafeAcp(std::size_t value)
    {
        return static_cast<std::int32_t>((std::min)(
            value,
            static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())));
    }

    CoreTextRange OrderedCoreTextRange(std::size_t first, std::size_t second)
    {
        return {
            SafeAcp((std::min)(first, second)),
            SafeAcp((std::max)(first, second)),
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
        activeContainer_ = session_->Selection().active.container_id;
        knownText_ = session_->TextInputTextUtf16(activeContainer_);
        knownLength_ = knownText_.size();
        knownRevision_ = session_->Revision();
        forceFullSynchronization_ = false;
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
        knownLength_ = 0;
        knownRevision_ = 0;
        activeContainer_ = {};
        knownText_.clear();
        forceFullSynchronization_ = false;
        pendingCharacterUpdate_.reset();
        committedCoreTextUpdate_.reset();
        lifetime_.reset();
    }

    void EditorTextInputController::FocusEnter()
    {
        if (!context_ || focused_) return;
        SynchronizeTextStore();
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
        if (session_->Selection().active.container_id != activeContainer_)
        {
            SynchronizeTextStore();
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

    void EditorTextInputController::BeginHardwareKey()
    {
        pendingCharacterUpdate_.reset();
        committedCoreTextUpdate_.reset();
    }

    bool EditorTextInputController::ConsumeCommittedCoreTextCharacter(std::u32string_view text)
    {
        if (!session_ || !committedCoreTextUpdate_) return false;
        auto const fresh = std::chrono::steady_clock::now()
                - committedCoreTextUpdate_->recordedAt
            < std::chrono::milliseconds(250);
        auto const matches = committedCoreTextUpdate_->revision == session_->Revision()
            && committedCoreTextUpdate_->text == text
            && fresh;
        committedCoreTextUpdate_.reset();
        return matches;
    }

    void EditorTextInputController::RecordCharacterTextUpdate(
        std::size_t start,
        std::u32string text)
    {
        if (!session_) return;
        pendingCharacterUpdate_ = CharacterUpdate{
            start,
            std::move(text),
            session_->Revision(),
            std::chrono::steady_clock::now(),
        };
        committedCoreTextUpdate_.reset();
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

        auto const container = session_->Selection().active.container_id;
        auto current = session_->TextInputTextUtf16(container);
        if (!forceFullSynchronization_
            && activeContainer_ == container
            && knownRevision_ == session_->Revision()
            && knownText_ == current)
        {
            NotifySelectionChanged();
            return;
        }

        notifying_ = true;
        auto const previousServiceLength = knownLength_;
        try
        {
            auto const selection = session_->Selection();
            auto const active = (std::min)(
                session_->TextInputAcpOffset(selection.active),
                current.size());
            auto const anchor = selection.anchor.container_id == container
                ? (std::min)(session_->TextInputAcpOffset(selection.anchor), current.size())
                : active;

            // CoreText may synchronously raise TextRequested,
            // SelectionRequested, or LayoutRequested while processing this
            // notification. The application text store must already expose
            // the post-edit text and selection when those callbacks run;
            // publishing the cache afterwards makes the notification's
            // replacement length disagree with TextRequested and causes
            // TextInputFramework to fail-fast. Keep the previous service
            // length only for the range being replaced.
            activeContainer_ = container;
            knownText_ = std::move(current);
            knownLength_ = knownText_.size();
            knownRevision_ = session_->Revision();
            forceFullSynchronization_ = false;
            context_.NotifyTextChanged(
                {0, SafeAcp(previousServiceLength)},
                SafeAcp(knownLength_),
                OrderedCoreTextRange(anchor, active));
        }
        catch (winrt::hresult_error const&)
        {
            // The editor remains authoritative even when the text service
            // rejects a transient notification. Preserve the new text for
            // re-entrant requests, but retain the service's old length so the
            // next full synchronization describes the correct replaced
            // range.
            knownLength_ = previousServiceLength;
            forceFullSynchronization_ = true;
        }
        notifying_ = false;
    }

    winrt::Windows::UI::Text::Core::CoreTextRange EditorTextInputController::CurrentSelection() const
    {
        if (!session_) return {};
        auto selection = session_->Selection();
        if (selection.active.container_id != activeContainer_) return {};
        const auto length = knownText_.size();
        const auto active = (std::min)(session_->TextInputAcpOffset(selection.active), length);
        const auto anchor = selection.anchor.container_id == activeContainer_
            ? (std::min)(session_->TextInputAcpOffset(selection.anchor), length)
            : active;
        return OrderedCoreTextRange(anchor, active);
    }

    void EditorTextInputController::RegisterHandlers()
    {
        textRequestedToken_ = context_.TextRequested([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextTextRequestedEventArgs const& args)
        {
            if (!session_) return;
            auto request = args.Request();
            auto range = request.Range();
            auto const& boundary = knownText_;
            auto textLength = SafeAcp(boundary.size());
            auto start = (std::max)(0, (std::min)(range.StartCaretPosition, textLength));
            auto end = (std::max)(start, (std::min)(range.EndCaretPosition, textLength));
            request.Text(winrt::hstring(
                boundary.data() + start,
                static_cast<uint32_t>(end - start)));
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
            auto length = knownText_.size();
            auto start = static_cast<std::size_t>((std::max)(0, selection.StartCaretPosition));
            auto end = static_cast<std::size_t>((std::max)(0, selection.EndCaretPosition));
            session_->SetSelection(
                session_->TextInputPositionFromAcp(activeContainer_, (std::min)(start, length)),
                session_->TextInputPositionFromAcp(activeContainer_, (std::min)(end, length)));
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
            auto requestedSelection = args.NewSelection();
            auto length = knownText_.size();
            auto start = static_cast<std::size_t>((std::max)(0, range.StartCaretPosition));
            auto end = static_cast<std::size_t>((std::max)(0, range.EndCaretPosition));
            auto incomingHstring = args.Text();
            auto incoming = elmd::utf8_to_cps(winrt::to_string(incomingHstring));
            auto isIncomingNewline = incoming == U"\r" || incoming == U"\n" || incoming == U"\r\n";
            if (!isIncomingNewline
                && pendingCharacterUpdate_
                && pendingCharacterUpdate_->revision == session_->Revision()
                && pendingCharacterUpdate_->start == start
                && pendingCharacterUpdate_->text == incoming
                && std::chrono::steady_clock::now()
                        - pendingCharacterUpdate_->recordedAt
                    < std::chrono::milliseconds(250))
            {
                pendingCharacterUpdate_.reset();
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
                QueueSynchronization();
                return;
            }
            pendingCharacterUpdate_.reset();
            auto selection = session_->Selection();
            auto const& text = knownText_;
            auto activeAcp = selection.active.container_id == activeContainer_
                ? session_->TextInputAcpOffset(selection.active)
                : 0u;
            if (isIncomingNewline && start < text.size() && text[start] == L'\n' && selection.is_caret() && activeAcp == start + 1)
            {
                // Enter is handled as a semantic structure command by the key
                // controller. Reject the duplicate CoreText update: accepting
                // it would make the text service advance a second newline.
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
                QueueSynchronization();
                return;
            }

            const auto storeStart = (std::min)(start, knownLength_);
            const auto storeEnd = (std::max)(storeStart, (std::min)(end, knownLength_));
            auto incomingText = std::wstring_view{incomingHstring.c_str(), incomingHstring.size()};
            auto previousKnownLength = knownLength_;
            auto previousKnownRevision = knownRevision_;
            auto previousKnownText = knownText_;
            auto previousActiveContainer = activeContainer_;
            auto previousForceFullSynchronization = forceFullSynchronization_;
            // Once this callback succeeds, CoreText will know the requested
            // replacement. Semantic marker conversions can produce a
            // different editor projection; QueueSynchronization reports that
            // extra delta only after this callback has returned.
            knownLength_ = knownLength_ - (storeEnd - storeStart) + incomingText.size();

            auto preserveCrossBlockSelection = selection.anchor.container_id
                    != selection.active.container_id
                && selection.active.container_id == activeContainer_
                && start == end
                && start == activeAcp;
            if (!preserveCrossBlockSelection)
            {
                session_->SetSelection(
                    session_->TextInputPositionFromAcp(activeContainer_, (std::min)(start, length)),
                    session_->TextInputPositionFromAcp(activeContainer_, (std::min)(end, length)));
            }
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
                knownLength_ = previousKnownLength;
                knownRevision_ = previousKnownRevision;
                knownText_ = std::move(previousKnownText);
                activeContainer_ = previousActiveContainer;
                forceFullSynchronization_ = previousForceFullSynchronization;
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
                QueueSynchronization();
                return;
            }
            auto nextSelection = session_->Selection();
            auto nextText = session_->TextInputTextUtf16(nextSelection.active.container_id);
            auto predicted = previousKnownText;
            predicted.replace(storeStart, storeEnd - storeStart, incomingText);
            if (nextSelection.active.container_id == previousActiveContainer
                && nextText == predicted)
            {
                auto const requestedStart = static_cast<std::size_t>((std::max)(
                    0,
                    requestedSelection.StartCaretPosition));
                auto const requestedEnd = static_cast<std::size_t>((std::max)(
                    0,
                    (std::max)(
                        requestedSelection.StartCaretPosition,
                        requestedSelection.EndCaretPosition)));
                auto const requestedAnchor = session_->TextInputPositionFromAcp(
                    previousActiveContainer,
                    (std::min)(requestedStart, nextText.size()));
                auto const requestedActive = session_->TextInputPositionFromAcp(
                    previousActiveContainer,
                    (std::min)(requestedEnd, nextText.size()));
                if (nextSelection.anchor != requestedAnchor
                    || nextSelection.active != requestedActive)
                {
                    session_->SetSelection(requestedAnchor, requestedActive);
                    if (render_) render_();
                }
                activeContainer_ = previousActiveContainer;
                knownText_ = std::move(nextText);
                knownLength_ = knownText_.size();
                knownRevision_ = session_->Revision();
                forceFullSynchronization_ = false;
            }
            else
            {
                forceFullSynchronization_ = true;
            }
            if (!incoming.empty() && !isIncomingNewline)
            {
                committedCoreTextUpdate_ = CharacterUpdate{
                    start,
                    incoming,
                    session_->Revision(),
                    std::chrono::steady_clock::now(),
                };
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

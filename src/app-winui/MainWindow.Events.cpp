#include "pch.h"
#include "MainWindow.xaml.h"

import folia.core.command;

namespace winrt::Folia::implementation
{
    void MainWindow::AttachControllers()
    {
        sidebarController.Attach(
            editorSession,
            editorRenderer,
            textInputController,
            OutlineList(),
            [this] { RenderEditorSurface(); });
        documentController.Attach(
            editorSession,
            editorRenderer,
            textInputController,
            [this](folia::Command const& command) { return ExecuteEditorCommand(command); },
            [this](winrt::hstring const& status) { SetStatus(status); },
            [this](bool active, std::optional<double> value, bool cancellable)
            {
                SetOperationProgress(active, value, cancellable);
            },
            [this]
            {
                latexCompletionController.Cancel();
                UpdateSourceModeUi();
                UpdateDocumentInfo();
                sidebarController.Refresh();
                if (FindReplaceBar().Visibility() == Microsoft::UI::Xaml::Visibility::Visible)
                    RefreshSearch(true);
                else
                    RenderEditorSurface();
            },
            [this] { RenderEditorSurface(); },
            [this] { return WindowHandle(); });
        keyboardController.Attach(
            editorSession,
            editorRenderer,
            textInputController,
            [this](folia::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { documentController.CopySelection(); },
            [this] { documentController.CutSelection(); },
            [this] { documentController.PasteClipboard(); },
            [this](std::string_view action)
            {
                if (action == "file.open") documentController.OpenDocument();
                else if (action == "file.save") documentController.SaveDocument();
                else if (action == "file.save_as") documentController.SaveDocumentAs();
                else if (action == "file.export_pdf") documentController.ExportPdf();
                else if (action == "search.find") ShowFindBar(false);
                else if (action == "search.replace") ShowFindBar(true);
                else if (action == "insert.image") documentController.InsertImage();
                else if (action == "view.source_mode") ToggleSourceMode();
            },
            appSettings.shortcutBindings,
            [this] { RenderEditorSurface(); });
        latexCompletionController.Attach(
            editorSession,
            editorRenderer,
            EditorSurface(),
            EditorOverlay(),
            LatexSuggestionPopup(),
            LatexSuggestionPrefix(),
            LatexSuggestionList(),
            latexCommandCatalog,
            [this](folia::NodeId container, folia::SourceRange replacement, std::u32string_view snippet)
            {
                return keyboardController.InsertSnippetReplacing(
                    container, replacement, snippet);
            });
        latexCompletionController.SetEnabled(appSettings.latexSuggestionsEnabled);
        pointerController.Attach(
            editorSession,
            editorRenderer,
            scrollController,
            textInputController,
            EditorSurface(),
            [this](folia::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { RenderEditorSurface(); },
            [this](std::string href) { documentController.OpenLink(std::move(href)); },
            [this](auto hit, auto position) { ShowFootnoteFlyout(hit, position); },
            [this]
            {
                keyboardController.ResetCaretGoal();
                keyboardController.CancelSnippetSession();
                latexCompletionController.Cancel();
            });
    }

    void MainWindow::RegisterWindowHandlers()
    {
        Closed([this](auto const&, auto const&)
        {
            if (latexCommandCatalog) latexCommandCatalog->Flush();
            if (settingsView) settingsView->Detach();
            documentController.Detach();
            sidebarController.Detach();
            pointerController.Detach();
            latexCompletionController.Detach();
            keyboardController.Detach();
            textInputController.Detach();
            scrollController.Detach();
        });

        SourceModeButton().Click([this](auto const&, auto const&)
        {
            ToggleSourceMode();
        });

        SidebarToggleButton().Click([this](auto const&, auto const&)
        {
            DocumentNavigation().IsPaneOpen(!DocumentNavigation().IsPaneOpen());
        });

        EditorSurface().Loaded([this](auto const&, auto const&)
        {
            InitializeEditorSurface();
        });

        EditorSurface().SizeChanged([this](auto const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
        {
            ResizeEditorSurface(args.NewSize().Width, args.NewSize().Height);
        });

        WorkspaceHost().SizeChanged([this](auto const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
        {
            OutlinePaneContent().Height((std::max)(0.0f, args.NewSize().Height));
        });

        EditorSurface().CompositionScaleChanged([this](auto const&, auto const&)
        {
            ResizeEditorSurface(EditorSurface().ActualWidth(), EditorSurface().ActualHeight());
        });

        Root().ActualThemeChanged([this](auto const&, auto const&)
        {
            if (appSettings.themeId == "system") UpdateTheme();
        });

        EditorSurface().IsTabStop(true);
        EditorSurface().CharacterReceived([this](auto const&, Microsoft::UI::Xaml::Input::CharacterReceivedRoutedEventArgs const& args)
        {
            auto handled = keyboardController.Character(args.Character());
            if (handled) latexCompletionController.Refresh();
            args.Handled(handled);
        });

        EditorSurface().KeyDown([this](auto const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
        {
            if (latexCompletionController.HandleKey(args.Key()))
            {
                args.Handled(true);
                return;
            }
            auto handled = keyboardController.Key(args.Key());
            if (handled) latexCompletionController.Refresh();
            args.Handled(handled);
        });

        auto enterAccelerator = Microsoft::UI::Xaml::Input::KeyboardAccelerator();
        enterAccelerator.Key(winrt::Windows::System::VirtualKey::Enter);
        enterAccelerator.Invoked([this](auto const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
        {
            if (latexCompletionController.AcceptSelected())
            {
                args.Handled(true);
                return;
            }
            auto handled = keyboardController.InsertNewline();
            if (handled) latexCompletionController.Refresh();
            args.Handled(handled);
        });
        EditorSurface().KeyboardAccelerators().Append(enterAccelerator);

        auto escapeAccelerator = Microsoft::UI::Xaml::Input::KeyboardAccelerator();
        escapeAccelerator.Key(winrt::Windows::System::VirtualKey::Escape);
        escapeAccelerator.Invoked([this](auto const&, auto const& args)
        {
            if (latexCompletionController.Active())
            {
                latexCompletionController.Cancel();
                args.Handled(true);
                return;
            }
            if (FindReplaceBar().Visibility() != Microsoft::UI::Xaml::Visibility::Visible) return;
            HideFindBar();
            args.Handled(true);
        });
        Root().KeyboardAccelerators().Append(escapeAccelerator);

        FindQueryBox().TextChanged([this](auto const&, auto const&) { RefreshSearch(true); });
        FindRegexButton().Click([this](auto const&, auto const&) { RefreshSearch(true); });
        FindCaseButton().Click([this](auto const&, auto const&) { RefreshSearch(true); });
        FindPreviousButton().Click([this](auto const&, auto const&) { NavigateSearch(-1); });
        FindNextButton().Click([this](auto const&, auto const&) { NavigateSearch(1); });
        CloseFindButton().Click([this](auto const&, auto const&) { HideFindBar(); });
        ReplaceCurrentButton().Click([this](auto const&, auto const&) { ReplaceCurrentSearchMatch(); });
        ReplaceAllButton().Click([this](auto const&, auto const&) { ReplaceAllSearchMatches(); });
        FindQueryBox().KeyDown([this](auto const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
        {
            if (args.Key() != winrt::Windows::System::VirtualKey::Enter) return;
            auto shift = Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(
                winrt::Windows::System::VirtualKey::Shift);
            NavigateSearch((static_cast<std::uint32_t>(shift) & 0x1u) != 0 ? -1 : 1);
            args.Handled(true);
        });
        ReplaceTextBox().KeyDown([this](auto const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
        {
            if (args.Key() != winrt::Windows::System::VirtualKey::Enter) return;
            ReplaceCurrentSearchMatch();
            args.Handled(true);
        });

        EditorSurface().PointerPressed([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            pointerController.PointerPressed(args);
        });

        EditorSurface().PointerMoved([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            pointerController.PointerMoved(args);
        });

        EditorSurface().PointerReleased([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            pointerController.PointerReleased(args);
        });

        EditorSurface().PointerExited([this](auto const&, auto const&)
        {
            pointerController.PointerExited();
        });

        EditorSurface().PointerCaptureLost([this](auto const&, auto const&)
        {
            pointerController.CancelPointerInteraction();
        });

        EditorSurface().PointerCanceled([this](auto const&, auto const&)
        {
            pointerController.CancelPointerInteraction();
        });

        EditorSurface().PointerWheelChanged([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            HandlePointerWheel(args);
        });

        EditorSurface().GotFocus([this](auto const&, auto const&)
        {
            textInputController.FocusEnter();
        });

        EditorSurface().LostFocus([this](auto const&, auto const&)
        {
            pointerController.CancelPointerInteraction();
            textInputController.FocusLeave();
        });

        EditorSurface().DoubleTapped([this](auto const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
        {
            pointerController.DoubleTapped(args);
        });

        OutlineList().SelectionChanged([this](auto const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
        {
            if (args.AddedItems().Size() > 0)
            {
                sidebarController.SelectOutline(args.AddedItems().GetAt(0));
            }
        });

    }
}

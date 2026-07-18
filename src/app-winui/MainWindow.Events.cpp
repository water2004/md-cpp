#include "pch.h"
#include "MainWindow.xaml.h"

import elmd.core.command;

namespace winrt::ElMd::implementation
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
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this](winrt::hstring const& status) { SetStatus(status); },
            [this](bool active, std::optional<double> value, bool cancellable)
            {
                SetOperationProgress(active, value, cancellable);
            },
            [this]
            {
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
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { documentController.CopySelection(); },
            [this] { documentController.CutSelection(); },
            [this] { documentController.PasteClipboard(); },
            [this] { RenderEditorSurface(); });
        pointerController.Attach(
            editorSession,
            editorRenderer,
            scrollController,
            textInputController,
            EditorSurface(),
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { RenderEditorSurface(); },
            [this](std::string href) { documentController.OpenLink(std::move(href)); },
            [this](auto hit, auto position) { ShowFootnoteFlyout(hit, position); },
            [this] { keyboardController.ResetCaretGoal(); });
    }

    void MainWindow::RegisterWindowHandlers()
    {
        Closed([this](auto const&, auto const&)
        {
            if (settingsView) settingsView->Detach();
            documentController.Detach();
            sidebarController.Detach();
            pointerController.Detach();
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
            args.Handled(keyboardController.Character(args.Character()));
        });

        EditorSurface().KeyDown([this](auto const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
        {
            args.Handled(keyboardController.Key(args.Key()));
        });

        auto enterAccelerator = Microsoft::UI::Xaml::Input::KeyboardAccelerator();
        enterAccelerator.Key(winrt::Windows::System::VirtualKey::Enter);
        enterAccelerator.Invoked([this](auto const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
        {
            args.Handled(keyboardController.InsertNewline());
        });
        EditorSurface().KeyboardAccelerators().Append(enterAccelerator);

        auto registerSearchAccelerator = [this](
            winrt::Windows::System::VirtualKey key,
            winrt::Windows::System::VirtualKeyModifiers modifiers,
            bool replace)
        {
            auto accelerator = Microsoft::UI::Xaml::Input::KeyboardAccelerator();
            accelerator.Key(key);
            accelerator.Modifiers(modifiers);
            accelerator.Invoked([this, replace](auto const&, auto const& args)
            {
                ShowFindBar(replace);
                args.Handled(true);
            });
            Root().KeyboardAccelerators().Append(accelerator);
        };
        registerSearchAccelerator(
            winrt::Windows::System::VirtualKey::F,
            winrt::Windows::System::VirtualKeyModifiers::Control,
            false);
        registerSearchAccelerator(
            winrt::Windows::System::VirtualKey::H,
            winrt::Windows::System::VirtualKeyModifiers::Control,
            true);

        auto escapeAccelerator = Microsoft::UI::Xaml::Input::KeyboardAccelerator();
        escapeAccelerator.Key(winrt::Windows::System::VirtualKey::Escape);
        escapeAccelerator.Invoked([this](auto const&, auto const& args)
        {
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

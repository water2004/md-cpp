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
                Title(L"el-md - " + editorSession.DisplayName());
                UpdateSourceModeUi();
                UpdateDocumentInfo();
                sidebarController.Refresh();
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

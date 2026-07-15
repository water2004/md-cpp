#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

import elmd.core.command;
import elmd.core.utf;

namespace winrt::ElMd::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        RegisterCommandHandlers();
        InitializeTextInput();
        sidebarController.Attach(
            editorSession,
            editorRenderer,
            textInputController,
            OutlineList(),
            DiagnosticsList(),
            [this] { RenderEditorSurface(); });
        documentController.Attach(
            editorSession,
            editorRenderer,
            textInputController,
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this](winrt::hstring const& status) { SetStatus(status); },
            [this]
            {
                Title(L"el-md - " + editorSession.DisplayName());
                UpdateSourceModeUi();
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

        Closed([this](auto const&, auto const&)
        {
            documentController.Detach();
            sidebarController.Detach();
            pointerController.Detach();
            keyboardController.Detach();
            textInputController.Detach();
            scrollController.Detach();
        });

        SidebarButton().Click([this](auto const&, auto const&)
        {
            auto checked = SidebarButton().IsChecked();
            SetSidebarExpanded(checked && checked.Value());
        });

        SourceModeButton().Click([this](auto const&, auto const&)
        {
            ToggleSourceMode();
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
            UpdateTheme();
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

        DiagnosticsList().SelectionChanged([this](auto const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
        {
            if (args.AddedItems().Size() > 0)
            {
                sidebarController.SelectDiagnostic(args.AddedItems().GetAt(0));
            }
        });

    }

    void MainWindow::RegisterCommandHandlers()
    {
        OpenButton().Click([this](auto const&, auto const&)
        {
            documentController.OpenDocument();
        });

        SaveButton().Click([this](auto const&, auto const&)
        {
            documentController.SaveDocument();
        });

        BoldButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleStrong;
            ExecuteEditorCommand(command);
        });

        ItalicButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleEmphasis;
            ExecuteEditorCommand(command);
        });

        StrikeButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleStrikethrough;
            ExecuteEditorCommand(command);
        });

        InlineCodeButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleInlineCode;
            ExecuteEditorCommand(command);
        });

        Heading1Button().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::SetHeading;
            command.level = 1;
            ExecuteEditorCommand(command);
        });

        Heading2Button().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::SetHeading;
            command.level = 2;
            ExecuteEditorCommand(command);
        });

        QuoteButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleBlockQuote;
            ExecuteEditorCommand(command);
        });

        UnorderedListButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleUnorderedList;
            ExecuteEditorCommand(command);
        });

        OrderedListButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleOrderedList;
            ExecuteEditorCommand(command);
        });

        TaskListButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleTaskList;
            ExecuteEditorCommand(command);
        });

        CodeBlockButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertCodeBlock;
            ExecuteEditorCommand(command);
        });

        TableButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertTable;
            command.rows = 2;
            command.cols = 3;
            ExecuteEditorCommand(command);
        });

        InlineMathButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertMathInline;
            ExecuteEditorCommand(command);
        });

        BlockMathButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertMathBlock;
            ExecuteEditorCommand(command);
        });

        LinkButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertLink;
            command.href = U"https://";
            ExecuteEditorCommand(command);
        });

        ImageButton().Click([this](auto const&, auto const&)
        {
            documentController.InsertImage();
        });

        FootnoteButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertFootnote;
            ExecuteEditorCommand(command);
        });

        auto toggleCallout = [this](std::u32string kind)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleCallout;
            command.callout_kind = std::move(kind);
            ExecuteEditorCommand(command);
        };
        CalloutNoteMenuItem().Click([toggleCallout](auto const&, auto const&) { toggleCallout(U"NOTE"); });
        CalloutTipMenuItem().Click([toggleCallout](auto const&, auto const&) { toggleCallout(U"TIP"); });
        CalloutWarningMenuItem().Click([toggleCallout](auto const&, auto const&) { toggleCallout(U"WARNING"); });

        TocButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertToc;
            ExecuteEditorCommand(command);
        });

        CutMenuItem().Click([this](auto const&, auto const&)
        {
            documentController.CutSelection();
        });

        CopyMenuItem().Click([this](auto const&, auto const&)
        {
            documentController.CopySelection();
        });

        PasteMenuItem().Click([this](auto const&, auto const&)
        {
            documentController.PasteClipboard();
        });

        SelectAllMenuItem().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::SelectAll;
            ExecuteEditorCommand(command);
        });
    }

    winrt::hstring MainWindow::DocumentStatus() const
    {
        auto status = editorSession.DisplayName()
            + L" | " + winrt::to_hstring(editorSession.TextView().size())
            + L" chars | rev " + winrt::to_hstring(editorSession.Revision());
        if (editorSession.IsSourceMode()) status = status + L" | Source";
        return status;
    }

    void MainWindow::UpdateSourceModeUi()
    {
        auto source = editorSession.IsSourceMode();
        SourceModeButton().IsChecked(source);
        BoldButton().IsEnabled(!source);
        ItalicButton().IsEnabled(!source);
        StrikeButton().IsEnabled(!source);
        InlineCodeButton().IsEnabled(!source);
        Heading1Button().IsEnabled(!source);
        Heading2Button().IsEnabled(!source);
        QuoteButton().IsEnabled(!source);
        UnorderedListButton().IsEnabled(!source);
        OrderedListButton().IsEnabled(!source);
        TaskListButton().IsEnabled(!source);
        CodeBlockButton().IsEnabled(!source);
        TableButton().IsEnabled(!source);
        InlineMathButton().IsEnabled(!source);
        BlockMathButton().IsEnabled(!source);
        LinkButton().IsEnabled(!source);
        ImageButton().IsEnabled(!source);
        FootnoteButton().IsEnabled(!source);
        CalloutButton().IsEnabled(!source);
        TocButton().IsEnabled(!source);
    }

    void MainWindow::ToggleSourceMode()
    {
        if (!editorSession.ToggleSourceMode())
        {
            UpdateSourceModeUi();
            return;
        }
        keyboardController.ResetCaretGoal();
        editorRenderer.ResetDocumentCaches();
        UpdateSourceModeUi();
        auto status = DocumentStatus();
        lastCommand = status;
        StatusText().Text(status);
        sidebarController.Refresh();
        textInputController.NotifyTextChanged();
        RenderEditorSurface();
        if (editorRenderer.ScrollToPosition(editorSession.Selection().active)) RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::InitializeTextInput()
    {
        textInputController.Attach(
            editorSession,
            editorRenderer,
            EditorSurface(),
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { RenderEditorSurface(); },
            [this] { return WindowHandle(); });

    }

    bool MainWindow::ExecuteEditorCommand(elmd::Command const& command)
    {
        keyboardController.ResetCaretGoal();
        auto oldRevision = editorSession.Revision();
        if (!editorSession.ExecuteCommand(command))
        {
            return false;
        }

        auto status = DocumentStatus();
        lastCommand = status;
        StatusText().Text(status);
        sidebarController.Refresh();
        RenderEditorSurface();
        if (editorRenderer.ScrollToPosition(editorSession.Selection().active)) RenderEditorSurface();
        if (editorSession.Revision() != oldRevision) textInputController.NotifyTextChanged();
        else textInputController.NotifySelectionChanged();
        return true;
    }

    void MainWindow::NavigateToFootnote(elmd::TextPosition position)
    {
        editorSession.SetSelection(position, position);
        textInputController.NotifySelectionChanged();
        editorRenderer.ScrollToPosition(position);
        RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::ShowFootnoteFlyout(
        winrt::ElMd::EditorSurfaceRenderer::FootnoteHit const& hit,
        Windows::Foundation::Point position)
    {
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        using namespace Microsoft::UI::Xaml::Controls::Primitives;

        if (footnoteFlyout) footnoteFlyout.Hide();
        footnoteFlyout = Flyout();

        StackPanel panel;
        panel.Spacing(8.0);
        panel.MaxWidth(360.0);

        TextBlock heading;
        heading.Text(L"[^" + winrt::to_hstring(hit.label) + L"]");
        heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        panel.Children().Append(heading);

        const auto isReference = hit.kind == winrt::ElMd::EditorFootnoteControlKind::Reference;
        auto target = isReference
            ? editorSession.FootnoteDefinitionTarget(hit.label)
            : editorSession.FirstFootnoteReferenceTarget(hit.label);
        TextBlock preview;
        preview.TextWrapping(TextWrapping::Wrap);
        preview.IsTextSelectionEnabled(false);
        if (isReference)
        {
            auto text = editorSession.FootnotePreview(hit.label);
            preview.Text(target
                ? text.empty() ? L"Empty footnote definition." : winrt::to_hstring(text)
                : L"This reference has no definition.");
        }
        else
        {
            preview.Text(target ? L"Return to the first reference." : L"This definition has no reference.");
        }
        panel.Children().Append(preview);

        Button action;
        if (target)
        {
            action.Content(winrt::box_value(isReference ? L"Go to definition" : L"Back to reference"));
            action.Click([this, target = *target](auto const&, auto const&)
            {
                if (footnoteFlyout) footnoteFlyout.Hide();
                NavigateToFootnote(target);
            });
        }
        else if (isReference)
        {
            action.Content(winrt::box_value(L"Create definition"));
            action.Click([this, label = hit.label](auto const&, auto const&)
            {
                if (footnoteFlyout) footnoteFlyout.Hide();
                elmd::Command command;
                command.kind = elmd::CommandKind::CreateFootnoteDefinition;
                command.footnote_label = label;
                ExecuteEditorCommand(command);
            });
        }
        if (target || isReference) panel.Children().Append(action);

        footnoteFlyout.Content(panel);
        FlyoutShowOptions options;
        options.Position(winrt::box_value(position).as<Windows::Foundation::IReference<Windows::Foundation::Point>>());
        options.ShowMode(FlyoutShowMode::Standard);
        footnoteFlyout.ShowAt(EditorSurface(), options);
    }

    void MainWindow::HandlePointerWheel(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto delta = args.GetCurrentPoint(EditorSurface()).Properties().MouseWheelDelta();
        scrollController.QueueScrollBy(static_cast<float>(-delta));
        args.Handled(true);
    }

    void MainWindow::SetStatus(winrt::hstring const& text)
    {
        lastCommand = text;
        StatusText().Text(text);
        RenderEditorSurface();
    }

    HWND MainWindow::WindowHandle()
    {
        HWND hwnd{};
        auto windowNative = get_strong().as<IWindowNative>();
        winrt::check_hresult(windowNative->get_WindowHandle(&hwnd));
        return hwnd;
    }

    void MainWindow::InitializeEditorSurface()
    {
        UpdateTheme();
        editorRenderer.SetInvalidateCallback([this] { RenderEditorSurface(); });
        editorRenderer.Initialize(EditorSurface());
        scrollController.Attach(editorRenderer, EditorScrollBar(), EditorScrollBarColumn(), WindowHandle(), [this] { RenderEditorSurface(); });
        RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::ResizeEditorSurface(double width, double height)
    {
        try
        {
            editorRenderer.Resize(EditorSurface(), width, height);
            RenderEditorSurface();
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Resize failed: " + error.message());
        }
    }

    void MainWindow::RenderEditorSurface()
    {
        try
        {
            editorRenderer.Render(editorSession.RenderFrame());
            scrollController.Sync();
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Render failed: " + error.message());
        }
    }

    void MainWindow::SetSidebarExpanded(bool expanded)
    {
        SidebarPanel().Visibility(expanded ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
        SidebarColumn().Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(expanded ? 280.0 : 0.0));
    }

    winrt::ElMd::EditorSurfaceRenderer::Theme MainWindow::CurrentRendererTheme()
    {
        return Root().ActualTheme() == Microsoft::UI::Xaml::ElementTheme::Dark
            ? winrt::ElMd::EditorSurfaceRenderer::Theme::Dark
            : winrt::ElMd::EditorSurfaceRenderer::Theme::Light;
    }

    void MainWindow::UpdateTheme()
    {
        editorRenderer.SetTheme(CurrentRendererTheme());
        RenderEditorSurface();
    }

}

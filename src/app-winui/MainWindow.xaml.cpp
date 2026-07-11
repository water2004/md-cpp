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
        documentController.Attach(
            editorSession,
            editorRenderer,
            textInputController,
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this](winrt::hstring const& status) { SetStatus(status); },
            [this]
            {
                Title(L"el-md - " + editorSession.DisplayName());
                UpdateOutlinePanel();
                UpdateDiagnosticsPanel();
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
            [this] { keyboardController.ResetCaretGoal(); });

        Closed([this](auto const&, auto const&)
        {
            documentController.Detach();
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
                HandleOutlineSelection(args.AddedItems().GetAt(0));
            }
        });

        DiagnosticsList().SelectionChanged([this](auto const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
        {
            if (args.AddedItems().Size() > 0)
            {
                HandleDiagnosticsSelection(args.AddedItems().GetAt(0));
            }
        });

        UpdateOutlinePanel();
        UpdateDiagnosticsPanel();
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

        CalloutButton().Click([this](auto const&, auto const&)
        {
            elmd::Command command;
            command.kind = elmd::CommandKind::ToggleCallout;
            command.callout_kind = U"NOTE";
            ExecuteEditorCommand(command);
        });

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
        auto oldTextLength = editorSession.Core().editor.text_cps().size();
        if (!editorSession.ExecuteCommand(command))
        {
            return false;
        }

        auto status = editorSession.DisplayName() + L" | " + winrt::to_hstring(editorSession.Text().size()) + L" chars | rev " + winrt::to_hstring(editorSession.Revision());
        lastCommand = status;
        StatusText().Text(status);
        UpdateOutlinePanel();
        UpdateDiagnosticsPanel();
        RenderEditorSurface();
        if (editorRenderer.ScrollToSourceOffset(editorSession.Core().editor.selection().active.v)) RenderEditorSurface();
        textInputController.NotifyTextChanged(oldTextLength);
        return true;
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
        scrollController.Attach(editorRenderer, EditorScrollBar(), EditorScrollBarColumn(), [this] { RenderEditorSurface(); });
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
            editorRenderer.Render(editorSession.Core());
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

    void MainWindow::UpdateOutlinePanel()
    {
        std::vector<std::size_t> headingOffsets;
        for (auto const& block : editorSession.Core().renderModel.blocks)
        {
            if (block.kind == elmd::RenderBlockKind::Text && block.block_style.margin_top >= 4.0f && block.content_range.end.v > block.content_range.start.v)
            {
                headingOffsets.push_back(block.content_range.start.v);
            }
        }

        std::vector<std::wstring> labels;
        std::vector<std::size_t> offsets;
        std::size_t headingIndex = 0;
        for (auto const* item : editorSession.Core().renderModel.outline.flat_items())
        {
            std::wstring indent((std::max)(0, static_cast<int>(item->level) - 1) * 2, L' ');
            auto title = winrt::to_hstring(item->title_plain_text);
            labels.push_back(indent + std::wstring(title.c_str()));
            offsets.push_back(headingIndex < headingOffsets.size() ? headingOffsets[headingIndex] : 0);
            ++headingIndex;
        }
        if (labels == outlineLabels && offsets == outlineOffsets) return;
        outlineLabels = std::move(labels);
        outlineOffsets = std::move(offsets);
        OutlineList().Items().Clear();
        for (auto const& label : outlineLabels) OutlineList().Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    void MainWindow::UpdateDiagnosticsPanel()
    {
        std::vector<std::wstring> labels;
        std::vector<std::size_t> offsets;
        for (auto const& diagnostic : editorSession.Core().renderModel.diagnostics)
        {
            auto severity = L"Warning";
            if (diagnostic.severity == elmd::RenderDiagnostic::Sev::Info)
            {
                severity = L"Info";
            }
            else if (diagnostic.severity == elmd::RenderDiagnostic::Sev::Error)
            {
                severity = L"Error";
            }

            auto offset = diagnostic.source_range ? diagnostic.source_range->start.v : 0;
            auto label = winrt::hstring(severity) + L" @ " + winrt::to_hstring(offset) + L": " + winrt::to_hstring(diagnostic.message);
            labels.emplace_back(label.c_str());
            offsets.push_back(offset);
        }

        if (labels.empty())
        {
            labels.push_back(L"No diagnostics");
            offsets.push_back(0);
        }
        if (labels == diagnosticLabels && offsets == diagnosticOffsets) return;
        diagnosticLabels = std::move(labels);
        diagnosticOffsets = std::move(offsets);
        DiagnosticsList().Items().Clear();
        for (auto const& label : diagnosticLabels) DiagnosticsList().Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    void MainWindow::HandleOutlineSelection(winrt::Windows::Foundation::IInspectable const& selectedItem)
    {
        auto selectedText = winrt::unbox_value<winrt::hstring>(selectedItem);
        for (uint32_t index = 0; index < OutlineList().Items().Size() && index < outlineOffsets.size(); ++index)
        {
            if (winrt::unbox_value<winrt::hstring>(OutlineList().Items().GetAt(index)) == selectedText)
            {
                editorSession.SetSelection(outlineOffsets[index], outlineOffsets[index]);
                textInputController.NotifySelectionChanged();
                editorRenderer.ScrollToSourceOffset(outlineOffsets[index]);
                RenderEditorSurface();
                return;
            }
        }
    }

    void MainWindow::HandleDiagnosticsSelection(winrt::Windows::Foundation::IInspectable const& selectedItem)
    {
        auto selectedText = winrt::unbox_value<winrt::hstring>(selectedItem);
        if (selectedText == L"No diagnostics")
        {
            return;
        }

        for (uint32_t index = 0; index < DiagnosticsList().Items().Size() && index < diagnosticOffsets.size(); ++index)
        {
            if (winrt::unbox_value<winrt::hstring>(DiagnosticsList().Items().GetAt(index)) == selectedText)
            {
                editorSession.SetSelection(diagnosticOffsets[index], diagnosticOffsets[index]);
                textInputController.NotifySelectionChanged();
                editorRenderer.ScrollToSourceOffset(diagnosticOffsets[index]);
                RenderEditorSurface();
                return;
            }
        }
    }
}

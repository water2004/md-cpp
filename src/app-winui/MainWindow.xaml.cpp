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
        pointerController.Attach(
            editorSession,
            editorRenderer,
            textInputController,
            EditorSurface(),
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { RenderEditorSurface(); },
            [this](std::string href) { OpenLinkAsync(std::move(href)); },
            [this] { caretGoalX = -1.0f; });

        Closed([this](auto const&, auto const&)
        {
            pointerController.Detach();
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
            args.Handled(HandleEditorCharacter(args.Character()));
        });

        EditorSurface().KeyDown([this](auto const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
        {
            args.Handled(HandleEditorKey(args.Key()));
        });

        auto enterAccelerator = Microsoft::UI::Xaml::Input::KeyboardAccelerator();
        enterAccelerator.Key(winrt::Windows::System::VirtualKey::Enter);
        enterAccelerator.Invoked([this](auto const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
        {
            args.Handled(InsertEditorNewline());
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
            OpenDocumentAsync();
        });

        SaveButton().Click([this](auto const&, auto const&)
        {
            SaveDocumentAsync();
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
            InsertImageAsync();
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
            CutSelectionToClipboard();
        });

        CopyMenuItem().Click([this](auto const&, auto const&)
        {
            CopySelectionToClipboard();
        });

        PasteMenuItem().Click([this](auto const&, auto const&)
        {
            PasteClipboardAsync();
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
        caretGoalX = -1.0f;
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

    bool MainWindow::InsertEditorNewline()
    {
        textInputController.ClearPendingCharacterTextUpdate();
        elmd::Command command;
        auto keyState = [](winrt::Windows::System::VirtualKey virtualKey)
        {
            return winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(virtualKey);
        };
        auto isDown = [&](winrt::Windows::System::VirtualKey virtualKey)
        {
            return (static_cast<std::uint32_t>(keyState(virtualKey)) & 0x1u) != 0;
        };
        auto shift = isDown(winrt::Windows::System::VirtualKey::Shift)
            || isDown(winrt::Windows::System::VirtualKey::LeftShift)
            || isDown(winrt::Windows::System::VirtualKey::RightShift);
        command.kind = shift ? elmd::CommandKind::InsertSoftBreak : elmd::CommandKind::InsertNewline;
        return ExecuteEditorCommand(command);
    }

    bool MainWindow::HandleEditorCharacter(char32_t character)
    {
        if (character == U'\r' || character == U'\n')
        {
            return InsertEditorNewline();
        }

        if (character < 0x20 || character == 0x7f)
        {
            return false;
        }

        auto start = editorSession.Core().editor.selection().normalized_range().start.v;
        std::u32string text(1, character);
        if (!ExecuteEditorCommand(elmd::Command::InsertText(text)))
        {
            return false;
        }
        textInputController.RecordCharacterTextUpdate(start, std::move(text));
        return true;
    }

    bool MainWindow::HandleEditorKey(winrt::Windows::System::VirtualKey key)
    {
        elmd::Command command;
        textInputController.ClearPendingCharacterTextUpdate();
        auto keyState = [](winrt::Windows::System::VirtualKey virtualKey)
        {
            return winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(virtualKey);
        };
        auto isDown = [&](winrt::Windows::System::VirtualKey virtualKey)
        {
            return (static_cast<std::uint32_t>(keyState(virtualKey)) & 0x1u) != 0;
        };
        auto ctrl = isDown(winrt::Windows::System::VirtualKey::Control)
            || isDown(winrt::Windows::System::VirtualKey::LeftControl)
            || isDown(winrt::Windows::System::VirtualKey::RightControl);
        auto shift = isDown(winrt::Windows::System::VirtualKey::Shift)
            || isDown(winrt::Windows::System::VirtualKey::LeftShift)
            || isDown(winrt::Windows::System::VirtualKey::RightShift);
        auto alt = isDown(winrt::Windows::System::VirtualKey::Menu)
            || isDown(winrt::Windows::System::VirtualKey::LeftMenu)
            || isDown(winrt::Windows::System::VirtualKey::RightMenu);

        if (ctrl)
        {
            switch (key)
            {
                case winrt::Windows::System::VirtualKey::Up:
                    command.kind = alt ? elmd::CommandKind::MoveTableRowUp : elmd::CommandKind::InsertTableRowAbove;
                    break;
                case winrt::Windows::System::VirtualKey::Down:
                    command.kind = alt ? elmd::CommandKind::MoveTableRowDown : elmd::CommandKind::InsertTableRowBelow;
                    break;
                case winrt::Windows::System::VirtualKey::Left:
                    command.kind = alt ? elmd::CommandKind::MoveTableColumnLeft : elmd::CommandKind::InsertTableColumnLeft;
                    break;
                case winrt::Windows::System::VirtualKey::Right:
                    command.kind = alt ? elmd::CommandKind::MoveTableColumnRight : elmd::CommandKind::InsertTableColumnRight;
                    break;
                case winrt::Windows::System::VirtualKey::Back:
                    command.kind = elmd::CommandKind::DeleteTableRow;
                    break;
                case winrt::Windows::System::VirtualKey::Delete:
                    command.kind = elmd::CommandKind::DeleteTableColumn;
                    break;
                case winrt::Windows::System::VirtualKey::Home:
                    command.kind = elmd::CommandKind::MoveDocumentStart;
                    command.extend_selection = shift;
                    break;
                case winrt::Windows::System::VirtualKey::End:
                    command.kind = elmd::CommandKind::MoveDocumentEnd;
                    command.extend_selection = shift;
                    break;
                case winrt::Windows::System::VirtualKey::Number1:
                    command.kind = elmd::CommandKind::SetHeading;
                    command.level = 1;
                    break;
                case winrt::Windows::System::VirtualKey::Number2:
                    command.kind = elmd::CommandKind::SetHeading;
                    command.level = 2;
                    break;
                case winrt::Windows::System::VirtualKey::Number7:
                    command.kind = elmd::CommandKind::ToggleOrderedList;
                    break;
                case winrt::Windows::System::VirtualKey::Number8:
                    command.kind = elmd::CommandKind::ToggleUnorderedList;
                    break;
                case winrt::Windows::System::VirtualKey::Number9:
                    command.kind = elmd::CommandKind::ToggleTaskList;
                    break;
                case winrt::Windows::System::VirtualKey::B:
                    command.kind = elmd::CommandKind::ToggleStrong;
                    break;
                case winrt::Windows::System::VirtualKey::I:
                    command.kind = elmd::CommandKind::ToggleEmphasis;
                    break;
                case winrt::Windows::System::VirtualKey::Q:
                    command.kind = elmd::CommandKind::ToggleBlockQuote;
                    break;
                case winrt::Windows::System::VirtualKey::T:
                    command.kind = elmd::CommandKind::InsertTable;
                    command.rows = 2;
                    command.cols = 3;
                    break;
                case winrt::Windows::System::VirtualKey::Z:
                    command.kind = shift ? elmd::CommandKind::Redo : elmd::CommandKind::Undo;
                    break;
                case winrt::Windows::System::VirtualKey::Y:
                    command.kind = elmd::CommandKind::Redo;
                    break;
                case winrt::Windows::System::VirtualKey::A:
                    command.kind = elmd::CommandKind::SelectAll;
                    break;
                case winrt::Windows::System::VirtualKey::C:
                    CopySelectionToClipboard();
                    return true;
                case winrt::Windows::System::VirtualKey::X:
                    CutSelectionToClipboard();
                    return true;
                case winrt::Windows::System::VirtualKey::V:
                    PasteClipboardAsync();
                    return true;
                default:
                    return false;
            }

            ExecuteEditorCommand(command);
            return true;
        }

        switch (key)
        {
            case winrt::Windows::System::VirtualKey::Back:
                command.kind = elmd::CommandKind::DeleteBackward;
                break;
            case winrt::Windows::System::VirtualKey::Delete:
                command.kind = elmd::CommandKind::DeleteForward;
                break;
            case winrt::Windows::System::VirtualKey::Enter:
                return InsertEditorNewline();
            case winrt::Windows::System::VirtualKey::Left:
                command.kind = elmd::CommandKind::MoveLeft;
                command.extend_selection = shift;
                caretGoalX = -1.0f;
                break;
            case winrt::Windows::System::VirtualKey::Right:
                command.kind = elmd::CommandKind::MoveRight;
                command.extend_selection = shift;
                caretGoalX = -1.0f;
                break;
            case winrt::Windows::System::VirtualKey::Up:
                return MoveCaretVerticalStep(false, shift);
            case winrt::Windows::System::VirtualKey::Down:
                return MoveCaretVerticalStep(true, shift);
            case winrt::Windows::System::VirtualKey::Home:
            {
                caretGoalX = -1.0f;
                auto selection = editorSession.Core().editor.selection();
                auto upstream = selection.affinity == elmd::TextAffinity::Upstream;
                if (auto offset = editorRenderer.VisualLineStart(selection.active.v, upstream))
                {
                    editorSession.SetSelection(shift ? selection.anchor.v : *offset, *offset, elmd::TextAffinity::Downstream);
                    textInputController.NotifySelectionChanged();
                    RenderEditorSurface();
                    if (editorRenderer.ScrollToSourceOffset(*offset)) RenderEditorSurface();
                    return true;
                }
                command.kind = elmd::CommandKind::MoveLineStart;
                command.extend_selection = shift;
                break;
            }
            case winrt::Windows::System::VirtualKey::End:
            {
                caretGoalX = -1.0f;
                auto selection = editorSession.Core().editor.selection();
                auto upstream = selection.affinity == elmd::TextAffinity::Upstream;
                if (auto offset = editorRenderer.VisualLineEnd(selection.active.v, upstream))
                {
                    editorSession.SetSelection(shift ? selection.anchor.v : *offset, *offset, elmd::TextAffinity::Upstream);
                    textInputController.NotifySelectionChanged();
                    RenderEditorSurface();
                    if (editorRenderer.ScrollToSourceOffset(*offset)) RenderEditorSurface();
                    return true;
                }
                command.kind = elmd::CommandKind::MoveLineEnd;
                command.extend_selection = shift;
                break;
            }
            case winrt::Windows::System::VirtualKey::Tab:
                command.kind = shift ? elmd::CommandKind::MoveTableCellPrevious : elmd::CommandKind::MoveTableCellNext;
                if (!ExecuteEditorCommand(command))
                {
                    ExecuteEditorCommand(elmd::Command::InsertText(U"\t"));
                }
                return true;
            default:
                return false;
        }

        ExecuteEditorCommand(command);
        return true;
    }

    bool MainWindow::MoveCaretVerticalStep(bool down, bool extend)
    {
        auto selection = editorSession.Core().editor.selection();
        auto upstream = selection.affinity == elmd::TextAffinity::Upstream;
        auto move = editorRenderer.MoveCaretVertically(selection.active.v, upstream, down, caretGoalX);
        if (!move)
        {
            caretGoalX = -1.0f;
            return false;
        }

        auto affinity = move->upstream ? elmd::TextAffinity::Upstream : elmd::TextAffinity::Downstream;
        editorSession.SetSelection(extend ? selection.anchor.v : move->offset, move->offset, affinity);
        textInputController.NotifySelectionChanged();
        RenderEditorSurface();
        if (editorRenderer.ScrollToSourceOffset(move->offset)) RenderEditorSurface();
        return true;
    }

    void MainWindow::HandlePointerWheel(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto delta = args.GetCurrentPoint(EditorSurface()).Properties().MouseWheelDelta();
        scrollController.QueueScrollBy(static_cast<float>(-delta));
        args.Handled(true);
    }

    winrt::fire_and_forget MainWindow::OpenLinkAsync(std::string href)
    {
        auto lifetime = get_strong();
        while (!href.empty() && static_cast<unsigned char>(href.front()) <= 0x20) href.erase(href.begin());
        while (!href.empty() && static_cast<unsigned char>(href.back()) <= 0x20) href.pop_back();
        if (href.empty()) co_return;
        if (href.front() == '#')
        {
            if (auto item = editorSession.Core().renderModel.outline.find_item_by_slug(href.substr(1)))
            {
                editorSession.SetSelection(item->source_range.start.v, item->source_range.start.v);
                textInputController.NotifySelectionChanged();
                editorRenderer.ScrollToSourceOffset(item->source_range.start.v);
                RenderEditorSurface();
            }
            co_return;
        }
        auto lower = href;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        try
        {
            if (lower.starts_with("https://") || lower.starts_with("http://") || lower.starts_with("mailto:"))
            {
                co_await winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(href)));
                co_return;
            }
            auto colon = lower.find(':');
            auto boundary = lower.find_first_of("/?#");
            if (colon != std::string::npos && (boundary == std::string::npos || colon < boundary)) co_return;
            auto path = std::filesystem::path(winrt::to_hstring(href).c_str());
            if (path.is_relative()) path = std::filesystem::path(editorSession.Core().baseDirectory) / path;
            auto file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(path.lexically_normal().wstring());
            co_await winrt::Windows::System::Launcher::LaunchFileAsync(file);
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Open link failed: " + error.message());
        }
    }

    void MainWindow::CopySelectionToClipboard()
    {
        if (!editorSession.HasSelection())
        {
            return;
        }

        auto package = winrt::Windows::ApplicationModel::DataTransfer::DataPackage();
        package.SetText(winrt::to_hstring(editorSession.SelectedTextUtf8()));
        winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        SetStatus(L"Copied selection");
    }

    void MainWindow::CutSelectionToClipboard()
    {
        if (!editorSession.HasSelection())
        {
            return;
        }

        CopySelectionToClipboard();
        elmd::Command command;
        command.kind = elmd::CommandKind::DeleteSelection;
        ExecuteEditorCommand(command);
    }

    winrt::fire_and_forget MainWindow::PasteClipboardAsync()
    {
        auto lifetime = get_strong();
        try
        {
            auto content = winrt::Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
            if (!content.Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text()))
            {
                co_return;
            }

            auto text = co_await content.GetTextAsync();
            if (!text.empty())
            {
                ExecuteEditorCommand(elmd::Command::InsertText(elmd::utf8_to_cps(winrt::to_string(text))));
            }
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Paste failed: " + error.message());
        }
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

    winrt::fire_and_forget MainWindow::InsertImageAsync()
    {
        auto lifetime = get_strong();
        try
        {
            auto picker = winrt::Windows::Storage::Pickers::FileOpenPicker();
            picker.FileTypeFilter().Append(L".png");
            picker.FileTypeFilter().Append(L".jpg");
            picker.FileTypeFilter().Append(L".jpeg");
            picker.FileTypeFilter().Append(L".gif");
            picker.FileTypeFilter().Append(L".webp");
            picker.FileTypeFilter().Append(L".bmp");
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHandle()));
            auto file = co_await picker.PickSingleFileAsync();
            if (!file) co_return;
            std::filesystem::path path(file.Path().c_str());
            if (editorSession.HasFile())
            {
                std::error_code error;
                auto base = std::filesystem::path(editorSession.Path().c_str()).parent_path();
                auto relative = std::filesystem::relative(path, base, error);
                if (!error && !relative.empty()) path = std::move(relative);
            }
            auto generic = path.generic_wstring();
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertImage;
            command.path = elmd::utf8_to_cps(winrt::to_string(winrt::hstring(generic)));
            ExecuteEditorCommand(command);
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Image insert failed: " + error.message());
        }
    }

    winrt::fire_and_forget MainWindow::OpenDocumentAsync()
    {
        auto lifetime = get_strong();

        try
        {
            auto picker = winrt::Windows::Storage::Pickers::FileOpenPicker();
            picker.FileTypeFilter().Append(L".md");
            picker.FileTypeFilter().Append(L".markdown");
            picker.FileTypeFilter().Append(L".txt");

            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHandle()));

            auto file = co_await picker.PickSingleFileAsync();
            if (!file)
            {
                SetStatus(L"Open cancelled");
                co_return;
            }

            auto text = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            SetStatus(L"Opening " + file.Name() + L"…");
            winrt::apartment_context uiContext;
            co_await winrt::resume_background();
            winrt::ElMd::EditorSession loaded;
            loaded.Open(file, text);
            co_await uiContext;
            editorSession = std::move(loaded);
            Title(L"el-md - " + editorSession.DisplayName());
            UpdateOutlinePanel();
            UpdateDiagnosticsPanel();
            SetStatus(editorSession.Path() + L" | " + winrt::to_hstring(editorSession.Text().size()) + L" chars | rev " + winrt::to_hstring(editorSession.Revision()));
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Open failed: " + error.message());
        }
    }

    winrt::fire_and_forget MainWindow::SaveDocumentAsync()
    {
        auto lifetime = get_strong();

        try
        {
            if (!editorSession.HasFile())
            {
                SaveDocumentAsAsync();
                co_return;
            }

            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(editorSession.File(), editorSession.Text());
            SetStatus(L"Saved " + editorSession.Path() + L" | " + winrt::to_hstring(editorSession.Text().size()) + L" chars | rev " + winrt::to_hstring(editorSession.Revision()));
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Save failed: " + error.message());
        }
    }

    winrt::fire_and_forget MainWindow::SaveDocumentAsAsync()
    {
        auto lifetime = get_strong();

        try
        {
            auto picker = winrt::Windows::Storage::Pickers::FileSavePicker();
            picker.DefaultFileExtension(L".md");
            picker.SuggestedFileName(L"Untitled.md");
            picker.FileTypeChoices().Insert(L"Markdown", winrt::single_threaded_vector<winrt::hstring>({ L".md" }));
            picker.FileTypeChoices().Insert(L"Text", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));

            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHandle()));

            auto file = co_await picker.PickSaveFileAsync();
            if (!file)
            {
                SetStatus(L"Save cancelled");
                co_return;
            }

            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, editorSession.Text());
            editorSession.SaveAs(file);
            Title(L"el-md - " + editorSession.DisplayName());
            SetStatus(L"Saved " + editorSession.Path() + L" | " + winrt::to_hstring(editorSession.Text().size()) + L" chars | rev " + winrt::to_hstring(editorSession.Revision()));
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Save failed: " + error.message());
        }
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

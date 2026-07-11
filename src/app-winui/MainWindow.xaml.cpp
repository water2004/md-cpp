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

        Closed([this](auto const&, auto const&) { StopScrollAnimation(); });

        EditorScrollBar().ValueChanged([this](auto const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
        {
            if (scrollBarUpdating) return;
            editorRenderer.SetScrollOffset(static_cast<float>(args.NewValue()));
            RenderEditorSurface();
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
            HandlePointerPressed(args);
        });

        EditorSurface().PointerMoved([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            HandlePointerMoved(args);
        });

        EditorSurface().PointerReleased([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            HandlePointerReleased(args);
        });

        EditorSurface().PointerExited([this](auto const&, auto const&)
        {
            hoverTooltip.reset();
            Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(EditorSurface(), nullptr);
            if (!tableDrag)
            {
                editorRenderer.ClearPointer();
                RenderEditorSurface();
            }
        });

        EditorSurface().PointerWheelChanged([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            HandlePointerWheel(args);
        });

        EditorSurface().GotFocus([this](auto const&, auto const&)
        {
            if (textEditContext)
            {
                textInputFocused = true;
                textEditContext.NotifyFocusEnter();
                textEditContext.NotifySelectionChanged(CurrentTextInputSelection());
            }
        });

        EditorSurface().LostFocus([this](auto const&, auto const&)
        {
            if (textEditContext)
            {
                textInputFocused = false;
                textEditContext.NotifyFocusLeave();
            }
        });

        EditorSurface().DoubleTapped([this](auto const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
        {
            HandleEditorDoubleTapped(args);
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
        auto manager = winrt::Windows::UI::Text::Core::CoreTextServicesManager::GetForCurrentView();
        textEditContext = manager.CreateEditContext();
        textEditContext.InputScope(winrt::Windows::UI::Text::Core::CoreTextInputScope::Text);
        textEditContext.InputPaneDisplayPolicy(winrt::Windows::UI::Text::Core::CoreTextInputPaneDisplayPolicy::Automatic);
        textInputKnownLength = editorSession.Core().editor.text_cps().size();

        textEditContext.TextRequested([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextTextRequestedEventArgs const& args)
        {
            auto request = args.Request();
            auto range = request.Range();
            auto text = editorSession.Text();
            auto textLength = static_cast<int32_t>(text.size());
            auto start = (std::max)(0, (std::min)(range.StartCaretPosition, textLength));
            auto end = (std::max)(start, (std::min)(range.EndCaretPosition, textLength));
            request.Text(winrt::hstring(text.c_str() + start, static_cast<uint32_t>(end - start)));
        });

        textEditContext.SelectionRequested([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextSelectionRequestedEventArgs const& args)
        {
            args.Request().Selection(CurrentTextInputSelection());
        });

        textEditContext.LayoutRequested([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextLayoutRequestedEventArgs const& args)
        {
            try
            {
                auto request = args.Request();
                auto selectionState = editorSession.Core().editor.selection();
                auto caret = selectionState.active.v;
                auto caretUpstream = selectionState.affinity == elmd::TextAffinity::Upstream;
                auto caretBounds = editorRenderer.CaretBounds(caret, caretUpstream);
                auto textRect = winrt::Windows::Foundation::Rect{ 0.0f, 0.0f, 1.0f, 24.0f };
                auto controlRect = winrt::Windows::Foundation::Rect{ 0.0f, 0.0f, static_cast<float>(EditorSurface().ActualWidth()), static_cast<float>(EditorSurface().ActualHeight()) };
                auto transform = EditorSurface().TransformToVisual(nullptr);
                auto hwnd = WindowHandle();
                POINT clientOrigin{};
                ClientToScreen(hwnd, &clientOrigin);
                auto dpi = static_cast<float>(GetDpiForWindow(hwnd));
                auto scale = dpi > 0.0f ? dpi / 96.0f : 1.0f;
                auto screenOrigin = winrt::Windows::Foundation::Point{ static_cast<float>(clientOrigin.x), static_cast<float>(clientOrigin.y) };
                if (caretBounds)
                {
                    auto point = transform.TransformPoint(winrt::Windows::Foundation::Point{ caretBounds->left, caretBounds->top });
                    textRect = winrt::Windows::Foundation::Rect{ screenOrigin.X + point.X * scale, screenOrigin.Y + point.Y * scale, (caretBounds->right - caretBounds->left) * scale, (caretBounds->bottom - caretBounds->top) * scale };
                }
                auto controlTopLeft = transform.TransformPoint(winrt::Windows::Foundation::Point{ 0.0f, 0.0f });
                controlRect.X = screenOrigin.X + controlTopLeft.X * scale;
                controlRect.Y = screenOrigin.Y + controlTopLeft.Y * scale;
                controlRect.Width *= scale;
                controlRect.Height *= scale;
                auto bounds = request.LayoutBounds();
                bounds.TextBounds(textRect);
                bounds.ControlBounds(controlRect);
            }
            catch (winrt::hresult_error const& error)
            {
                (void)error;
            }
        });

        textEditContext.SelectionUpdating([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextSelectionUpdatingEventArgs const& args)
        {
            auto selection = args.Selection();
            auto length = editorSession.Core().editor.text_cps().size();
            auto start = static_cast<std::size_t>((std::max)(0, selection.StartCaretPosition));
            auto end = static_cast<std::size_t>((std::max)(0, selection.EndCaretPosition));
            editorSession.SetSelection((std::min)(start, length), (std::min)(end, length));
            RenderEditorSurface();
            args.Result(winrt::Windows::UI::Text::Core::CoreTextSelectionUpdatingResult::Succeeded);
        });

        textEditContext.TextUpdating([this](auto const&, winrt::Windows::UI::Text::Core::CoreTextTextUpdatingEventArgs const& args)
        {
            auto range = args.Range();
            auto length = editorSession.Core().editor.text_cps().size();
            auto start = static_cast<std::size_t>((std::max)(0, range.StartCaretPosition));
            auto end = static_cast<std::size_t>((std::max)(0, range.EndCaretPosition));
            auto incoming = elmd::utf8_to_cps(winrt::to_string(args.Text()));
            auto isIncomingNewline = incoming == U"\r" || incoming == U"\n" || incoming == U"\r\n";
            auto selection = editorSession.Core().editor.selection();
            auto text = editorSession.Core().editor.text_cps();
            if (isIncomingNewline
                && start < text.size()
                && text[start] == U'\n'
                && selection.is_caret()
                && selection.active.v == start + 1)
            {
                RenderEditorSurface();
                textEditContext.NotifySelectionChanged(CurrentTextInputSelection());
                textInputKnownLength = length;
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
                return;
            }
            if (pendingCharacterTextUpdate
                && start <= text.size()
                && start == pendingCharacterStart
                && incoming == pendingCharacterText
                && start + pendingCharacterText.size() <= text.size()
                && selection.is_caret()
                && selection.active.v == start + pendingCharacterText.size()
                && std::equal(pendingCharacterText.begin(), pendingCharacterText.end(), text.begin() + start))
            {
                pendingCharacterTextUpdate = false;
                auto newSelection = args.NewSelection();
                auto newStart = static_cast<std::size_t>((std::max)(0, newSelection.StartCaretPosition));
                auto newEnd = static_cast<std::size_t>((std::max)(0, newSelection.EndCaretPosition));
                editorSession.SetSelection((std::min)(newStart, length), (std::min)(newEnd, length));
                RenderEditorSurface();
                textEditContext.NotifySelectionChanged(CurrentTextInputSelection());
                textInputKnownLength = length;
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
                return;
            }
            pendingCharacterTextUpdate = false;
            editorSession.SetSelection((std::min)(start, length), (std::min)(end, length));
            auto command = isIncomingNewline ? elmd::Command{} : elmd::Command::InsertText(incoming);
            if (isIncomingNewline)
            {
                command.kind = elmd::CommandKind::InsertNewline;
            }
            textInputUpdating = true;
            if (ExecuteEditorCommand(command))
            {
                textInputUpdating = false;
                auto newLength = editorSession.Core().editor.text_cps().size();
                if (!isIncomingNewline)
                {
                    auto newSelection = args.NewSelection();
                    auto newStart = static_cast<std::size_t>((std::max)(0, newSelection.StartCaretPosition));
                    auto newEnd = static_cast<std::size_t>((std::max)(0, newSelection.EndCaretPosition));
                    editorSession.SetSelection((std::min)(newStart, newLength), (std::min)(newEnd, newLength));
                }
                RenderEditorSurface();
                textEditContext.NotifySelectionChanged(CurrentTextInputSelection());
                textInputKnownLength = newLength;
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Succeeded);
            }
            else
            {
                textInputUpdating = false;
                args.Result(winrt::Windows::UI::Text::Core::CoreTextTextUpdatingResult::Failed);
            }
        });
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
        NotifyTextInputChanged(oldTextLength);
        return true;
    }

    bool MainWindow::InsertEditorNewline()
    {
        pendingCharacterTextUpdate = false;
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

    winrt::Windows::UI::Text::Core::CoreTextRange MainWindow::CurrentTextInputSelection() const
    {
        auto selection = editorSession.Core().editor.selection();
        return winrt::Windows::UI::Text::Core::CoreTextRange{ static_cast<int32_t>(selection.anchor.v), static_cast<int32_t>(selection.active.v) };
    }

    void MainWindow::NotifyTextInputChanged(std::size_t oldLength)
    {
        if (!textEditContext || textInputUpdating)
        {
            return;
        }

        auto newLength = editorSession.Core().editor.text_cps().size();
        textEditContext.NotifyTextChanged(
            winrt::Windows::UI::Text::Core::CoreTextRange{ 0, static_cast<int32_t>(oldLength) },
            static_cast<int32_t>(newLength),
            CurrentTextInputSelection());
        textEditContext.NotifySelectionChanged(CurrentTextInputSelection());
        textInputKnownLength = newLength;
    }

    void MainWindow::NotifyTextInputSelectionChanged()
    {
        if (textEditContext)
        {
            textEditContext.NotifySelectionChanged(CurrentTextInputSelection());
        }
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
        pendingCharacterTextUpdate = true;
        pendingCharacterStart = start;
        pendingCharacterText = std::move(text);
        return true;
    }

    bool MainWindow::HandleEditorKey(winrt::Windows::System::VirtualKey key)
    {
        elmd::Command command;
        pendingCharacterTextUpdate = false;
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
                    NotifyTextInputSelectionChanged();
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
                    NotifyTextInputSelectionChanged();
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
        NotifyTextInputSelectionChanged();
        RenderEditorSurface();
        if (editorRenderer.ScrollToSourceOffset(move->offset)) RenderEditorSurface();
        return true;
    }

    void MainWindow::HandlePointerPressed(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Pointer);
        caretGoalX = -1.0f;
        auto point = args.GetCurrentPoint(EditorSurface()).Position();
        editorRenderer.UpdatePointer(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (auto action = editorRenderer.TableActionAt(static_cast<float>(point.X), static_cast<float>(point.Y)))
        {
            if (action->kind == winrt::ElMd::EditorSurfaceRenderer::TableActionKind::DragRow || action->kind == winrt::ElMd::EditorSurfaceRenderer::TableActionKind::DragColumn)
            {
                tableDrag = action;
                auto rows = action->kind == winrt::ElMd::EditorSurfaceRenderer::TableActionKind::DragRow;
                tableDropIndex = editorRenderer.TableDropIndexAt(static_cast<float>(point.X), static_cast<float>(point.Y), rows);
                editorRenderer.SetTableDrag(tableDrag, tableDropIndex);
                EditorSurface().CapturePointer(args.Pointer());
                RenderEditorSurface();
                args.Handled(true);
                return;
            }

            editorSession.SetSelection(action->sourceOffset, action->sourceOffset);
            NotifyTextInputSelectionChanged();
            elmd::Command command;
            command.table_index = action->index;
            switch (action->kind)
            {
                case winrt::ElMd::EditorSurfaceRenderer::TableActionKind::InsertRow:
                    command.kind = elmd::CommandKind::InsertTableRowAt;
                    break;
                case winrt::ElMd::EditorSurfaceRenderer::TableActionKind::InsertColumn:
                    command.kind = elmd::CommandKind::InsertTableColumnAt;
                    break;
                case winrt::ElMd::EditorSurfaceRenderer::TableActionKind::DeleteRow:
                    command.kind = elmd::CommandKind::DeleteTableRow;
                    break;
                case winrt::ElMd::EditorSurfaceRenderer::TableActionKind::DeleteColumn:
                    command.kind = elmd::CommandKind::DeleteTableColumn;
                    break;
                default:
                    return;
            }
            ExecuteEditorCommand(command);
            args.Handled(true);
            return;
        }
        bool hitUpstream = false;
        auto hit = editorRenderer.HitTest(static_cast<float>(point.X), static_cast<float>(point.Y), &hitUpstream);
        if (!hit)
        {
            return;
        }

        if ((args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Control) == winrt::Windows::System::VirtualKeyModifiers::Control)
        {
            if (auto link = LinkAtSourceOffset(*hit))
            {
                OpenLinkAsync(std::move(*link));
                args.Handled(true);
                return;
            }
        }

        if (TryToggleTaskCheckboxAt(*hit))
        {
            args.Handled(true);
            return;
        }

        pointerSelecting = true;
        pointerAnchor = *hit;
        editorSession.SetSelection(pointerAnchor, pointerAnchor, hitUpstream ? elmd::TextAffinity::Upstream : elmd::TextAffinity::Downstream);
        NotifyTextInputSelectionChanged();
        EditorSurface().CapturePointer(args.Pointer());
        RenderEditorSurface();
        args.Handled(true);
    }

    void MainWindow::HandlePointerMoved(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto point = args.GetCurrentPoint(EditorSurface()).Position();
        editorRenderer.UpdatePointer(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (!pointerSelecting && !tableDrag)
        {
            auto hit = editorRenderer.HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
            auto tooltip = hit ? TooltipAtSourceOffset(*hit) : std::nullopt;
            if (tooltip != hoverTooltip)
            {
                hoverTooltip = tooltip;
                Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                    EditorSurface(),
                    tooltip ? winrt::box_value(winrt::to_hstring(*tooltip)) : nullptr);
            }
        }
        if (tableDrag)
        {
            auto rows = tableDrag->kind == winrt::ElMd::EditorSurfaceRenderer::TableActionKind::DragRow;
            tableDropIndex = editorRenderer.TableDropIndexAt(static_cast<float>(point.X), static_cast<float>(point.Y), rows);
            editorRenderer.SetTableDrag(tableDrag, tableDropIndex);
            RenderEditorSurface();
            args.Handled(true);
            return;
        }
        if (!pointerSelecting)
        {
            RenderEditorSurface();
            return;
        }
        bool hitUpstream = false;
        auto hit = editorRenderer.HitTest(static_cast<float>(point.X), static_cast<float>(point.Y), &hitUpstream);
        if (!hit)
        {
            return;
        }

        editorSession.SetSelection(pointerAnchor, *hit, hitUpstream ? elmd::TextAffinity::Upstream : elmd::TextAffinity::Downstream);
        NotifyTextInputSelectionChanged();
        RenderEditorSurface();
        args.Handled(true);
    }

    void MainWindow::HandlePointerReleased(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (tableDrag)
        {
            auto action = *tableDrag;
            auto dropIndex = tableDropIndex;
            tableDrag.reset();
            tableDropIndex.reset();
            editorRenderer.SetTableDrag(std::nullopt, std::nullopt);
            EditorSurface().ReleasePointerCapture(args.Pointer());
            if (dropIndex)
            {
                editorSession.SetSelection(action.sourceOffset, action.sourceOffset);
                NotifyTextInputSelectionChanged();
                elmd::Command command;
                command.kind = action.kind == winrt::ElMd::EditorSurfaceRenderer::TableActionKind::DragRow
                    ? elmd::CommandKind::MoveTableRowTo
                    : elmd::CommandKind::MoveTableColumnTo;
                command.table_index = *dropIndex;
                ExecuteEditorCommand(command);
            }
            else
            {
                RenderEditorSurface();
            }
            args.Handled(true);
            return;
        }
        if (pointerSelecting)
        {
            pointerSelecting = false;
            EditorSurface().ReleasePointerCapture(args.Pointer());
            args.Handled(true);
        }
    }

    void MainWindow::HandlePointerWheel(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto delta = args.GetCurrentPoint(EditorSurface()).Properties().MouseWheelDelta();
        editorRenderer.QueueScrollBy(static_cast<float>(-delta));
        StartScrollAnimation();
        args.Handled(true);
    }

    void MainWindow::HandleEditorDoubleTapped(Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
    {
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Pointer);
        auto point = args.GetPosition(EditorSurface());
        auto hit = editorRenderer.HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (hit && SelectWordAt(*hit))
        {
            RenderEditorSurface();
            args.Handled(true);
        }
    }

    bool MainWindow::SelectWordAt(std::size_t offset)
    {
        auto text = editorSession.Core().editor.text_cps();
        if (text.empty())
        {
            return false;
        }

        auto isWordChar = [](char32_t ch)
        {
            return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z') || (ch >= U'0' && ch <= U'9') || ch == U'_' || ch > 0x7f;
        };

        if (offset >= text.size())
        {
            offset = text.size() - 1;
        }
        if (!isWordChar(text[offset]) && offset > 0 && isWordChar(text[offset - 1]))
        {
            --offset;
        }
        if (!isWordChar(text[offset]))
        {
            return false;
        }

        auto start = offset;
        while (start > 0 && isWordChar(text[start - 1]))
        {
            --start;
        }

        auto end = offset + 1;
        while (end < text.size() && isWordChar(text[end]))
        {
            ++end;
        }

        editorSession.SetSelection(start, end);
        NotifyTextInputSelectionChanged();
        return true;
    }

    bool MainWindow::TryToggleTaskCheckboxAt(std::size_t offset)
    {
        auto text = editorSession.Core().editor.text_cps();
        auto lineStart = (std::min)(offset, text.size());
        while (lineStart > 0 && text[lineStart - 1] != U'\n')
        {
            --lineStart;
        }

        auto pos = lineStart;
        while (pos < text.size() && (text[pos] == U' ' || text[pos] == U'\t'))
        {
            ++pos;
        }

        if (pos + 5 >= text.size() || (text[pos] != U'-' && text[pos] != U'*' && text[pos] != U'+') || text[pos + 1] != U' ' || text[pos + 2] != U'[' || text[pos + 4] != U']' || text[pos + 5] != U' ')
        {
            return false;
        }
        if (offset < pos || offset > pos + 5)
        {
            return false;
        }

        editorSession.SetSelection(offset, offset);
        NotifyTextInputSelectionChanged();
        elmd::Command command;
        command.kind = elmd::CommandKind::ToggleTaskCheckbox;
        return ExecuteEditorCommand(command);
    }

    std::optional<std::string> MainWindow::LinkAtSourceOffset(std::size_t offset) const
    {
        auto scanItems = [&](auto& self, auto const& items) -> std::optional<std::string>
        {
            for (auto const& item : items)
            {
                if (item.kind == elmd::InlineRenderItem::Kind::Link && item.source_range.start.v <= offset && offset <= item.source_range.end.v) return item.href;
                if (!item.children.empty()) if (auto nested = self(self, item.children)) return nested;
            }
            return std::nullopt;
        };
        auto scanBlock = [&](auto& self, auto const& block) -> std::optional<std::string>
        {
            if (block.kind == elmd::RenderBlockKind::Image && block.link && block.source_range.start.v <= offset && offset <= block.source_range.end.v) return *block.link;
            if (auto link = scanItems(scanItems, block.inline_items)) return link;
            if (block.callout_title) if (auto link = scanItems(scanItems, *block.callout_title)) return link;
            for (auto const& cell : block.table_cells) if (auto link = scanItems(scanItems, cell)) return link;
            for (auto const& child : block.child_blocks) if (auto link = self(self, child)) return link;
            return std::nullopt;
        };
        for (auto const& block : editorSession.Core().renderModel.blocks) if (auto link = scanBlock(scanBlock, block)) return link;
        return std::nullopt;
    }

    std::optional<std::string> MainWindow::TooltipAtSourceOffset(std::size_t offset) const
    {
        auto scanItems = [&](auto& self, auto const& items) -> std::optional<std::string>
        {
            for (auto const& item : items)
            {
                if (item.source_range.start.v <= offset && offset <= item.source_range.end.v)
                {
                    if (item.title && !item.title->empty()) return *item.title;
                    if (item.kind == elmd::InlineRenderItem::Kind::Link && !item.href.empty()) return item.href;
                    if (item.kind == elmd::InlineRenderItem::Kind::Image && !item.alt.empty()) return item.alt;
                }
                if (!item.children.empty()) if (auto nested = self(self, item.children)) return nested;
            }
            return std::nullopt;
        };
        auto scanBlock = [&](auto& self, auto const& block) -> std::optional<std::string>
        {
            if (block.kind == elmd::RenderBlockKind::Image && block.source_range.start.v <= offset && offset <= block.source_range.end.v)
            {
                if (block.title && !block.title->empty()) return *block.title;
                if (!block.alt.empty()) return block.alt;
            }
            if (auto tooltip = scanItems(scanItems, block.inline_items)) return tooltip;
            if (block.callout_title) if (auto tooltip = scanItems(scanItems, *block.callout_title)) return tooltip;
            for (auto const& cell : block.table_cells) if (auto tooltip = scanItems(scanItems, cell)) return tooltip;
            for (auto const& child : block.child_blocks) if (auto tooltip = self(self, child)) return tooltip;
            return std::nullopt;
        };
        for (auto const& block : editorSession.Core().renderModel.blocks) if (auto tooltip = scanBlock(scanBlock, block)) return tooltip;
        return std::nullopt;
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
                NotifyTextInputSelectionChanged();
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
        editorRenderer.InitializeMermaid(MermaidWebView());
        editorRenderer.Initialize(EditorSurface());
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
            UpdateEditorScrollBar();
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Render failed: " + error.message());
        }
    }

    void MainWindow::UpdateEditorScrollBar()
    {
        scrollBarUpdating = true;
        auto maximum = static_cast<double>(editorRenderer.MaximumScrollOffset());
        auto viewport = static_cast<double>(editorRenderer.ViewportHeight());
        if (std::fabs(EditorScrollBar().Maximum() - maximum) > 0.5) EditorScrollBar().Maximum(maximum);
        if (std::fabs(EditorScrollBar().ViewportSize() - viewport) > 0.5) EditorScrollBar().ViewportSize(viewport);
        auto largeChange = (std::max)(48.0, viewport * 0.9);
        if (std::fabs(EditorScrollBar().LargeChange() - largeChange) > 0.5) EditorScrollBar().LargeChange(largeChange);
        auto value = static_cast<double>((std::min)(editorRenderer.ScrollOffset(), static_cast<float>(maximum)));
        if (std::fabs(EditorScrollBar().Value() - value) > 0.1) EditorScrollBar().Value(value);
        auto visible = maximum > 0.5;
        auto visibility = visible ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed;
        if (EditorScrollBar().Visibility() != visibility)
        {
            EditorScrollBar().Visibility(visibility);
            EditorScrollBarColumn().Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(visible ? 16.0 : 0.0));
        }
        scrollBarUpdating = false;
    }

    void MainWindow::StartScrollAnimation()
    {
        lastScrollFrame = std::chrono::steady_clock::now();
        if (scrollRenderingActive) return;
        scrollRenderingToken = Microsoft::UI::Xaml::Media::CompositionTarget::Rendering({ this, &MainWindow::OnScrollFrame });
        scrollRenderingActive = true;
    }

    void MainWindow::StopScrollAnimation()
    {
        if (!scrollRenderingActive) return;
        Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(scrollRenderingToken);
        scrollRenderingActive = false;
    }

    void MainWindow::OnScrollFrame(Windows::Foundation::IInspectable const&, Windows::Foundation::IInspectable const&)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = lastScrollFrame.time_since_epoch().count() == 0
            ? 1.0f / 60.0f
            : std::chrono::duration<float>(now - lastScrollFrame).count();
        lastScrollFrame = now;
        auto active = editorRenderer.AdvanceScrollAnimation((std::min)(elapsed, 0.05f));
        RenderEditorSurface();
        if (!active) StopScrollAnimation();
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
                NotifyTextInputSelectionChanged();
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
                NotifyTextInputSelectionChanged();
                editorRenderer.ScrollToSourceOffset(diagnosticOffsets[index]);
                RenderEditorSurface();
                return;
            }
        }
    }
}

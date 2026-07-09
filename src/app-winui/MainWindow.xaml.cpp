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

        EditorSurface().IsTabStop(true);
        EditorSurface().CharacterReceived([this](auto const&, Microsoft::UI::Xaml::Input::CharacterReceivedRoutedEventArgs const& args)
        {
            HandleEditorCharacter(args.Character());
            args.Handled(true);
        });

        EditorSurface().KeyDown([this](auto const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
        {
            HandleEditorKey(args.Key());
            args.Handled(true);
        });

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

        EditorSurface().PointerWheelChanged([this](auto const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            HandlePointerWheel(args);
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
    }

    bool MainWindow::ExecuteEditorCommand(elmd::Command const& command)
    {
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
        editorRenderer.ScrollToSourceOffset(editorSession.Core().editor.selection().active.v);
        RenderEditorSurface();
        return true;
    }

    void MainWindow::HandleEditorCharacter(char32_t character)
    {
        if (character < 0x20 || character == 0x7f)
        {
            return;
        }

        ExecuteEditorCommand(elmd::Command::InsertText(std::u32string(1, character)));
    }

    void MainWindow::HandleEditorKey(winrt::Windows::System::VirtualKey key)
    {
        elmd::Command command;
        auto keyState = [](winrt::Windows::System::VirtualKey virtualKey)
        {
            return winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(virtualKey);
        };
        auto isDown = [&](winrt::Windows::System::VirtualKey virtualKey)
        {
            return (static_cast<std::uint32_t>(keyState(virtualKey)) & 0x1u) != 0;
        };
        auto ctrl = isDown(winrt::Windows::System::VirtualKey::Control);
        auto shift = isDown(winrt::Windows::System::VirtualKey::Shift);

        if (ctrl)
        {
            switch (key)
            {
                case winrt::Windows::System::VirtualKey::Home:
                    command.kind = elmd::CommandKind::MoveDocumentStart;
                    command.extend_selection = shift;
                    break;
                case winrt::Windows::System::VirtualKey::End:
                    command.kind = elmd::CommandKind::MoveDocumentEnd;
                    command.extend_selection = shift;
                    break;
                case winrt::Windows::System::VirtualKey::B:
                    command.kind = elmd::CommandKind::ToggleStrong;
                    break;
                case winrt::Windows::System::VirtualKey::I:
                    command.kind = elmd::CommandKind::ToggleEmphasis;
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
                    return;
                case winrt::Windows::System::VirtualKey::X:
                    CutSelectionToClipboard();
                    return;
                case winrt::Windows::System::VirtualKey::V:
                    PasteClipboardAsync();
                    return;
                default:
                    return;
            }

            ExecuteEditorCommand(command);
            return;
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
                command.kind = elmd::CommandKind::InsertNewline;
                break;
            case winrt::Windows::System::VirtualKey::Left:
                command.kind = elmd::CommandKind::MoveLeft;
                command.extend_selection = shift;
                break;
            case winrt::Windows::System::VirtualKey::Right:
                command.kind = elmd::CommandKind::MoveRight;
                command.extend_selection = shift;
                break;
            case winrt::Windows::System::VirtualKey::Up:
                command.kind = elmd::CommandKind::MoveUp;
                command.extend_selection = shift;
                break;
            case winrt::Windows::System::VirtualKey::Down:
                command.kind = elmd::CommandKind::MoveDown;
                command.extend_selection = shift;
                break;
            case winrt::Windows::System::VirtualKey::Home:
                command.kind = elmd::CommandKind::MoveLineStart;
                command.extend_selection = shift;
                break;
            case winrt::Windows::System::VirtualKey::End:
                command.kind = elmd::CommandKind::MoveLineEnd;
                command.extend_selection = shift;
                break;
            case winrt::Windows::System::VirtualKey::Tab:
                command = elmd::Command::InsertText(U"\t");
                break;
            default:
                return;
        }

        ExecuteEditorCommand(command);
    }

    void MainWindow::HandlePointerPressed(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Pointer);
        auto point = args.GetCurrentPoint(EditorSurface()).Position();
        auto hit = editorRenderer.HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (!hit)
        {
            return;
        }

        pointerSelecting = true;
        pointerAnchor = *hit;
        editorSession.SetSelection(pointerAnchor, pointerAnchor);
        EditorSurface().CapturePointer(args.Pointer());
        RenderEditorSurface();
        args.Handled(true);
    }

    void MainWindow::HandlePointerMoved(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!pointerSelecting)
        {
            return;
        }

        auto point = args.GetCurrentPoint(EditorSurface()).Position();
        auto hit = editorRenderer.HitTest(static_cast<float>(point.X), static_cast<float>(point.Y));
        if (!hit)
        {
            return;
        }

        editorSession.SetSelection(pointerAnchor, *hit);
        RenderEditorSurface();
        args.Handled(true);
    }

    void MainWindow::HandlePointerReleased(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
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
        editorRenderer.ScrollBy(static_cast<float>(-delta));
        RenderEditorSurface();
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
        return true;
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
            editorSession.Open(file, text);
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
        editorRenderer.Initialize(EditorSurface());
        RenderEditorSurface();
    }

    void MainWindow::ResizeEditorSurface(double width, double height)
    {
        editorRenderer.Resize(EditorSurface(), width, height);
        RenderEditorSurface();
    }

    void MainWindow::RenderEditorSurface()
    {
        editorRenderer.Render(editorSession.Core());
    }

    void MainWindow::UpdateOutlinePanel()
    {
        OutlineList().Items().Clear();
        outlineOffsets.clear();
        std::vector<std::size_t> headingOffsets;
        for (auto const& block : editorSession.Core().renderModel.blocks)
        {
            if (block.kind == elmd::RenderBlockKind::Text && block.block_style.margin_top >= 4.0f && block.content_range.end.v > block.content_range.start.v)
            {
                headingOffsets.push_back(block.content_range.start.v);
            }
        }

        std::size_t headingIndex = 0;
        for (auto const* item : editorSession.Core().renderModel.outline.flat_items())
        {
            std::wstring indent((std::max)(0, static_cast<int>(item->level) - 1) * 2, L' ');
            OutlineList().Items().Append(winrt::box_value(winrt::hstring(indent + winrt::to_hstring(item->title_plain_text).c_str())));
            outlineOffsets.push_back(headingIndex < headingOffsets.size() ? headingOffsets[headingIndex] : 0);
            ++headingIndex;
        }
    }

    void MainWindow::UpdateDiagnosticsPanel()
    {
        DiagnosticsList().Items().Clear();
        diagnosticOffsets.clear();
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
            DiagnosticsList().Items().Append(winrt::box_value(label));
            diagnosticOffsets.push_back(offset);
        }

        if (DiagnosticsList().Items().Size() == 0)
        {
            DiagnosticsList().Items().Append(winrt::box_value(L"No diagnostics"));
            diagnosticOffsets.push_back(editorSession.Core().editor.selection().active.v);
        }
    }

    void MainWindow::HandleOutlineSelection(winrt::Windows::Foundation::IInspectable const& selectedItem)
    {
        auto selectedText = winrt::unbox_value<winrt::hstring>(selectedItem);
        for (uint32_t index = 0; index < OutlineList().Items().Size() && index < outlineOffsets.size(); ++index)
        {
            if (winrt::unbox_value<winrt::hstring>(OutlineList().Items().GetAt(index)) == selectedText)
            {
                editorSession.SetSelection(outlineOffsets[index], outlineOffsets[index]);
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
                editorRenderer.ScrollToSourceOffset(diagnosticOffsets[index]);
                RenderEditorSurface();
                return;
            }
        }
    }
}

#include "pch.h"
#include "MainWindow.xaml.h"

import elmd.core.command;

namespace winrt::ElMd::implementation
{
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

        ExportPdfButton().Click([this](auto const&, auto const&)
        {
            documentController.ExportPdf();
        });

        SettingsButton().Click([this](auto const&, auto const&)
        {
            ToggleSettingsMode();
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
}

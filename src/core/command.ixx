// elmd.core.command — Command enum + to_transaction() translation.
// Faithful port of editor-core::command. Some commands return std::nullopt
// (unimplemented in v1) and are handled at the editor/UI layer (HANDOFF).
export module elmd.core.command;
import std;
import elmd.core.dialect;

export namespace elmd {

enum class CommandKind {
    InsertText, DeleteBackward, DeleteForward, DeleteSelection, InsertNewline, InsertSoftBreak,
    MoveLeft, MoveRight, MoveUp, MoveDown,
    MoveLineStart, MoveLineEnd, MoveDocumentStart, MoveDocumentEnd,
    MovePageUp, MovePageDown, SelectAll,
    ToggleStrong, ToggleEmphasis, ToggleStrikethrough, ToggleInlineCode,
    SetHeading, ClearHeading,
    ToggleUnorderedList, ToggleOrderedList, ToggleTaskList, ToggleTaskCheckbox, ToggleBlockQuote,
    InsertCodeBlock, InsertMathInline, InsertMathBlock,
    InsertTable, InsertImage, InsertLink, InsertFootnote, InsertToc,
    MoveTableCellNext, MoveTableCellPrevious,
    InsertTableRowAbove, InsertTableRowBelow, DeleteTableRow, MoveTableRowUp, MoveTableRowDown,
    InsertTableColumnLeft, InsertTableColumnRight, DeleteTableColumn, MoveTableColumnLeft, MoveTableColumnRight,
    SetTableColumnAlignment, NormalizeTable,
    InsertTableRowAt, InsertTableColumnAt, MoveTableRowTo, MoveTableColumnTo,
    ToggleCallout, ExtensionCmd,
    Undo, Redo, Cut, Copy, Paste,
};

struct Command {
    CommandKind kind = CommandKind::InsertText;
    std::u32string text{};                 // InsertText / Paste / Extension payload
    bool extend_selection = false;         // Move*
    std::uint8_t level = 0;               // SetHeading
    std::optional<std::u32string> lang;    // InsertCodeBlock
    std::size_t rows = 0, cols = 0;       // InsertTable
    TableAlignment table_alignment = TableAlignment::None;
    std::size_t table_index = 0;
    std::u32string path, alt;              // InsertImage
    std::u32string href;                   // InsertLink
    std::optional<std::u32string> title;    // InsertLink
    std::u32string callout_kind;           // ToggleCallout
    std::u32string ext_name;               // Extension

    static Command InsertText(std::u32string t) { Command c; c.kind = CommandKind::InsertText; c.text = std::move(t); return c; }
    static Command MoveLeft(bool ext)  { Command c; c.kind = CommandKind::MoveLeft;  c.extend_selection = ext; return c; }
    static Command MoveRight(bool ext) { Command c; c.kind = CommandKind::MoveRight; c.extend_selection = ext; return c; }
    static Command MoveUp(bool ext)    { Command c; c.kind = CommandKind::MoveUp;    c.extend_selection = ext; return c; }
    static Command MoveDown(bool ext)  { Command c; c.kind = CommandKind::MoveDown;  c.extend_selection = ext; return c; }
    // ...other factories are small enough to inline at call-sites.
};

} // namespace elmd

// elmd.core.command — Command enum + to_transaction() translation.
// Faithful port of editor-core::command. Some commands return std::nullopt
// (unimplemented in v1) and are handled at the editor/UI layer (HANDOFF).
export module elmd.core.command;
import std;
import elmd.core.types;
import elmd.core.selection;
import elmd.core.transaction;
import elmd.core.utf;
import elmd.core.dialect;

export namespace elmd {

enum class CommandKind {
    InsertText, DeleteBackward, DeleteForward, DeleteSelection, InsertNewline,
    MoveLeft, MoveRight, MoveUp, MoveDown,
    MoveLineStart, MoveLineEnd, MoveDocumentStart, MoveDocumentEnd,
    MovePageUp, MovePageDown, SelectAll,
    ToggleStrong, ToggleEmphasis, ToggleStrikethrough, ToggleInlineCode,
    SetHeading, ClearHeading,
    ToggleUnorderedList, ToggleOrderedList, ToggleTaskList, ToggleBlockQuote,
    InsertCodeBlock, InsertMathInline, InsertMathBlock,
    InsertTable, InsertImage, InsertLink, InsertFootnote, InsertToc,
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

inline std::size_t find_line_start(const std::u32string& cps, std::size_t pos) {
    std::size_t p = std::min(pos, cps.size());
    while (p > 0 && cps[p - 1] != '\n') --p;
    return p;
}
inline std::size_t find_line_end(const std::u32string& cps, std::size_t pos) {
    std::size_t p = std::min(pos, cps.size());
    while (p < cps.size() && cps[p] != '\n') ++p;
    return p;
}

struct MarkdownNewlineEdit {
    CharRange range;
    std::u32string text;
    CharOffset caret;
};

inline bool is_space_or_tab(char32_t ch) {
    return ch == U' ' || ch == U'\t';
}

inline std::optional<MarkdownNewlineEdit> markdown_newline_edit(const std::u32string& cps, CharRange sel) {
    std::size_t ls = find_line_start(cps, sel.start.v);
    std::size_t le = find_line_end(cps, sel.start.v);
    std::u32string line(cps.begin() + ls, cps.begin() + le);
    std::size_t pos = 0;
    std::u32string base_prefix;
    while (pos < line.size() && is_space_or_tab(line[pos])) base_prefix.push_back(line[pos++]);
    while (pos < line.size() && line[pos] == U'>') {
        base_prefix.push_back(U'>');
        ++pos;
        if (pos < line.size() && line[pos] == U' ') {
            base_prefix.push_back(U' ');
            ++pos;
        } else {
            base_prefix.push_back(U' ');
        }
        while (pos < line.size() && is_space_or_tab(line[pos])) base_prefix.push_back(line[pos++]);
    }

    std::u32string marker;
    if (pos + 1 < line.size() && (line[pos] == U'-' || line[pos] == U'*' || line[pos] == U'+') && line[pos + 1] == U' ') {
        marker.push_back(line[pos]);
        marker.push_back(U' ');
        pos += 2;
    } else {
        std::size_t number_start = pos;
        while (pos < line.size() && line[pos] >= U'0' && line[pos] <= U'9') ++pos;
        if (pos > number_start && pos + 1 < line.size() && (line[pos] == U'.' || line[pos] == U')') && line[pos + 1] == U' ') {
            std::size_t value = 0;
            for (std::size_t i = number_start; i < pos; ++i) value = value * 10 + static_cast<std::size_t>(line[i] - U'0');
            marker = utf8_to_cps(std::to_string(value + 1));
            marker.push_back(line[pos]);
            marker.push_back(U' ');
            pos += 2;
        }
    }

    if (base_prefix.empty() && marker.empty()) return std::nullopt;
    bool empty_item = true;
    for (std::size_t i = pos; i < line.size(); ++i) {
        if (!is_space_or_tab(line[i])) {
            empty_item = false;
            break;
        }
    }

    if (empty_item && (!marker.empty() || !base_prefix.empty())) {
        return MarkdownNewlineEdit{CharRange(CharOffset(ls), CharOffset(le)), U"", CharOffset(ls)};
    }

    std::u32string insert;
    insert.push_back(U'\n');
    insert += base_prefix;
    insert += marker;
    return MarkdownNewlineEdit{sel, insert, CharOffset(sel.start.v + insert.size())};
}

inline std::size_t markdown_line_prefix_end(const std::u32string& line) {
    std::size_t pos = 0;
    while (pos < line.size() && is_space_or_tab(line[pos])) ++pos;
    if (pos + 5 < line.size() && line[pos] == U'-' && line[pos + 1] == U' ' && line[pos + 2] == U'[' && (line[pos + 3] == U' ' || line[pos + 3] == U'x' || line[pos + 3] == U'X') && line[pos + 4] == U']' && line[pos + 5] == U' ') return pos + 6;
    if (pos + 1 < line.size() && (line[pos] == U'-' || line[pos] == U'*' || line[pos] == U'+') && line[pos + 1] == U' ') return pos + 2;
    std::size_t number_start = pos;
    while (pos < line.size() && line[pos] >= U'0' && line[pos] <= U'9') ++pos;
    if (pos > number_start && pos + 1 < line.size() && (line[pos] == U'.' || line[pos] == U')') && line[pos + 1] == U' ') return pos + 2;
    return number_start;
}

inline std::optional<Transaction> list_toggle_transaction(const Command& cmd, const std::u32string& text_cps, const Selection& selection, std::uint64_t revision) {
    auto sel = selection.normalized_range();
    std::size_t ls = find_line_start(text_cps, sel.start.v);
    std::size_t le = find_line_end(text_cps, sel.end.v);
    std::u32string line(text_cps.begin() + ls, text_cps.begin() + le);
    std::size_t indent_end = 0;
    while (indent_end < line.size() && is_space_or_tab(line[indent_end])) ++indent_end;
    std::size_t prefix_end = markdown_line_prefix_end(line);
    std::u32string body(line.begin() + prefix_end, line.end());
    std::u32string prefix(line.begin(), line.begin() + indent_end);
    bool already_unordered = prefix_end == indent_end + 2 && indent_end + 1 < line.size() && (line[indent_end] == U'-' || line[indent_end] == U'*' || line[indent_end] == U'+') && line[indent_end + 1] == U' ';
    bool already_task = prefix_end == indent_end + 6 && indent_end + 5 < line.size() && line[indent_end] == U'-' && line[indent_end + 1] == U' ' && line[indent_end + 2] == U'[' && line[indent_end + 4] == U']' && line[indent_end + 5] == U' ';
    bool already_ordered = prefix_end > indent_end + 2 && prefix_end <= line.size() && (line[prefix_end - 2] == U'.' || line[prefix_end - 2] == U')') && line[prefix_end - 1] == U' ';
    std::u32string new_line;
    if ((cmd.kind == CommandKind::ToggleUnorderedList && already_unordered) || (cmd.kind == CommandKind::ToggleOrderedList && already_ordered) || (cmd.kind == CommandKind::ToggleTaskList && already_task)) {
        new_line = prefix + body;
    } else if (cmd.kind == CommandKind::ToggleOrderedList) {
        new_line = prefix + U"1. " + body;
    } else if (cmd.kind == CommandKind::ToggleTaskList) {
        new_line = prefix + U"- [ ] " + body;
    } else {
        new_line = prefix + U"- " + body;
    }
    std::size_t new_prefix_len = new_line.size() - body.size();
    std::size_t body_offset = selection.head().v > ls + prefix_end ? (std::min)(selection.head().v - ls - prefix_end, body.size()) : 0;
    std::size_t new_pos = ls + new_prefix_len + body_offset;
    Transaction t(revision, selection, Selection::caret(CharOffset(new_pos)), TransactionReason::StructuralCommand);
    t.with_edit(CharRange(CharOffset(ls), CharOffset(le)), std::move(new_line));
    return t;
}

// to_transaction: builds (does not apply) a Transaction. `text_cps` is the
// current buffer text as codepoints; `revision` is buffer.revision().
inline std::optional<Transaction> to_transaction(const Command& cmd,
                                                 const std::u32string& text_cps,
                                                 const Selection& selection,
                                                 std::uint64_t revision) {
    auto sel = selection.normalized_range();
    std::size_t len = text_cps.size();
    auto caret_after = [&](CharOffset p) { return Selection::caret(p); };
    auto extend_after = [&](CharOffset p) {
        Selection s; s.anchor = selection.anchor; s.active = p;
        s.affinity = (p.v < selection.anchor.v) ? TextAffinity::Upstream : TextAffinity::Downstream;
        return s;
    };

    switch (cmd.kind) {
        case CommandKind::InsertText: {
            std::size_t new_pos = sel.start.v + cmd.text.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::Typing);
            t.with_edit(sel, cmd.text);
            return t;
        }
        case CommandKind::DeleteBackward: {
            if (!selection.is_caret()) return to_transaction(Command{CommandKind::DeleteSelection}, text_cps, selection, revision);
            std::size_t pos = sel.start.v;
            if (pos == 0) return std::nullopt;
            std::size_t prev = prev_grapheme_boundary_char(text_cps, pos);
            Transaction t(revision, selection, caret_after(CharOffset(prev)), TransactionReason::Delete);
            t.with_edit(CharRange(CharOffset(prev), CharOffset(pos)), U"");
            return t;
        }
        case CommandKind::DeleteForward: {
            if (!selection.is_caret()) return to_transaction(Command{CommandKind::DeleteSelection}, text_cps, selection, revision);
            std::size_t pos = sel.start.v;
            if (pos >= len) return std::nullopt;
            std::size_t nxt = next_grapheme_boundary_char(text_cps, pos);
            Transaction t(revision, selection, caret_after(CharOffset(pos)), TransactionReason::Delete);
            t.with_edit(CharRange(CharOffset(pos), CharOffset(nxt)), U"");
            return t;
        }
        case CommandKind::DeleteSelection: {
            if (selection.is_caret()) return std::nullopt;
            Transaction t(revision, selection, caret_after(sel.start), TransactionReason::Delete);
            t.with_edit(sel, U"");
            return t;
        }
        case CommandKind::InsertNewline: {
            auto markdown_edit = selection.is_caret() ? markdown_newline_edit(text_cps, sel) : std::nullopt;
            Transaction t(revision, selection, caret_after(markdown_edit ? markdown_edit->caret : CharOffset(sel.start.v + 1)), TransactionReason::Typing);
            if (markdown_edit) t.with_edit(markdown_edit->range, markdown_edit->text);
            else t.with_edit(sel, U"\n");
            return t;
        }
        case CommandKind::MoveRight: {
            std::size_t nxt = std::min(next_grapheme_boundary_char(text_cps, sel.start.v), len);
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(nxt)) : caret_after(CharOffset(nxt)), TransactionReason::Typing);
        }
        case CommandKind::MoveLeft: {
            std::size_t prev = sel.start.v > 0 ? prev_grapheme_boundary_char(text_cps, sel.start.v) : 0;
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(prev)) : caret_after(CharOffset(prev)), TransactionReason::Typing);
        }
        case CommandKind::MoveLineStart: {
            std::size_t p = find_line_start(text_cps, sel.start.v);
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(p)) : caret_after(CharOffset(p)), TransactionReason::Typing);
        }
        case CommandKind::MoveLineEnd: {
            std::size_t p = find_line_end(text_cps, sel.end.v);
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(p)) : caret_after(CharOffset(p)), TransactionReason::Typing);
        }
        case CommandKind::MoveDocumentStart: {
            std::size_t p = 0;
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(p)) : caret_after(CharOffset(p)), TransactionReason::Typing);
        }
        case CommandKind::MoveDocumentEnd: {
            std::size_t p = len;
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(p)) : caret_after(CharOffset(p)), TransactionReason::Typing);
        }
        case CommandKind::MoveUp: {
            std::size_t cur = sel.start.v;
            std::size_t ls = find_line_start(text_cps, cur);
            std::size_t col = cur - ls;
            std::size_t target;
            if (ls == 0) target = 0;
            else {
                std::size_t prev_end = ls - 1;
                std::size_t prev_start = find_line_start(text_cps, prev_end);
                target = prev_start + std::min(col, prev_end - prev_start);
            }
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(target)) : caret_after(CharOffset(target)), TransactionReason::Typing);
        }
        case CommandKind::MoveDown: {
            std::size_t cur = sel.start.v;
            std::size_t ls = find_line_start(text_cps, cur);
            std::size_t col = cur - ls;
            std::size_t le = find_line_end(text_cps, cur);
            std::size_t target;
            if (le >= len) target = len;
            else {
                std::size_t next_start = le + 1;
                std::size_t next_end = find_line_end(text_cps, next_start);
                target = next_start + std::min(col, next_end - next_start);
            }
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(target)) : caret_after(CharOffset(target)), TransactionReason::Typing);
        }
        case CommandKind::MovePageUp:
        case CommandKind::MovePageDown:
            return std::nullopt; // unimplemented
        case CommandKind::ToggleStrong:
        case CommandKind::ToggleEmphasis:
        case CommandKind::ToggleStrikethrough:
        case CommandKind::ToggleInlineCode: {
            if (sel.is_empty()) {
                // no-op transaction: editor gate will drop it.
                return Transaction(revision, selection, selection, TransactionReason::FormatCommand);
            }
            auto wrap = [&](char32_t a, char32_t b) {
                std::u32string w; w.push_back(a); w.push_back(b);
                if (a == b) { w.clear(); w.push_back(a); w.push_back(a); }
                auto sel_text = std::u32string(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
                std::u32string wrapped;
                if (a == '`') { wrapped.push_back('`'); wrapped += sel_text; wrapped.push_back('`'); }
                else if (a == '*') { wrapped.push_back('*'); wrapped.push_back('*'); wrapped += sel_text; wrapped.push_back('*'); wrapped.push_back('*'); }
                else if (a == '~') { wrapped.push_back('~'); wrapped.push_back('~'); wrapped += sel_text; wrapped.push_back('~'); wrapped.push_back('~'); }
                else if (a == '_') { wrapped.push_back('_'); wrapped.push_back('_'); wrapped += sel_text; wrapped.push_back('_'); wrapped.push_back('_'); }
                (void)b;
                std::size_t new_pos = sel.start.v + wrapped.size();
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(wrapped));
                return t;
            };
            char32_t a = '`', b = '`';
            if (cmd.kind == CommandKind::ToggleStrong)        { a = '*'; b = '*'; }
            else if (cmd.kind == CommandKind::ToggleEmphasis)  { a = '*'; b = '`'; }   // single * effectively; handled below
            else if (cmd.kind == CommandKind::ToggleStrikethrough) { a = '~'; b = '~'; }
            else if (cmd.kind == CommandKind::ToggleInlineCode){ a = '`'; b = '`'; }
            // For ToggleEmphasis use single '*' wrap (HANDOFF note: only ever wraps).
            auto wrap_emph = [&]() {
                std::u32string sel_text(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
                std::u32string wrapped; wrapped.push_back('*'); wrapped += sel_text; wrapped.push_back('*');
                std::size_t new_pos = sel.start.v + wrapped.size();
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(wrapped));
                return t;
            };
            if (cmd.kind == CommandKind::ToggleEmphasis) return wrap_emph();
            return wrap(a, b);
        }
        case CommandKind::InsertMathInline: {
            if (sel.is_empty()) {
                std::u32string tmpl; tmpl.push_back('$'); tmpl.push_back(' '); tmpl.push_back(' '); tmpl.push_back('$');
                std::size_t new_pos = sel.start.v + 2;
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(tmpl));
                return t;
            } else {
                auto sel_text = std::u32string(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
                std::u32string wrapped; wrapped.push_back('$'); wrapped += sel_text; wrapped.push_back('$');
                std::size_t new_pos = sel.start.v + wrapped.size();
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(wrapped));
                return t;
            }
        }
        case CommandKind::InsertMathBlock: {
            std::u32string block; block.push_back('\n'); block += U"$$\n\n$$\n";
            std::size_t new_pos = sel.start.v + 4;
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
            t.with_edit(sel, std::move(block));
            return t;
        }
        case CommandKind::InsertToc: {
            std::u32string s = U"\n[TOC]\n";
            std::size_t new_pos = sel.start.v + s.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::StructuralCommand);
            t.with_edit(sel, s);
            return t;
        }
        case CommandKind::InsertTable: {
            std::u32string s;
            for (std::size_t c = 0; c < cmd.cols; ++c) s += U"| Header ";
            s.push_back('|'); s.push_back('\n');
            for (std::size_t c = 0; c < cmd.cols; ++c) s += U"|---";
            s.push_back('|'); s.push_back('\n');
            for (std::size_t r = 0; r < cmd.rows; ++r) {
                for (std::size_t c = 0; c < cmd.cols; ++c) s += U"| Cell ";
                s.push_back('|'); s.push_back('\n');
            }
            std::size_t new_pos = sel.start.v + s.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::StructuralCommand);
            t.with_edit(sel, std::move(s));
            return t;
        }
        case CommandKind::SetHeading: {
            std::size_t ls = find_line_start(text_cps, sel.start.v);
            std::size_t le = find_line_end(text_cps, sel.end.v);
            std::u32string line_text(text_cps.begin() + ls, text_cps.begin() + le);
            // strip leading '#' chars
            std::size_t off = 0; while (off < line_text.size() && line_text[off] == '#') ++off;
            std::u32string stripped(line_text.begin() + off, line_text.end());
            // trim leading spaces
            std::size_t sp = 0; while (sp < stripped.size() && (stripped[sp] == ' ' || stripped[sp] == '\t')) ++sp;
            stripped = stripped.substr(sp);
            std::u32string prefix; for (std::uint8_t i = 0; i < cmd.level; ++i) prefix.push_back('#'); prefix.push_back(' ');
            std::u32string new_line = prefix + stripped;
            std::size_t new_pos = ls + new_line.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::StructuralCommand);
            t.with_edit(CharRange(CharOffset(ls), CharOffset(le)), std::move(new_line));
            return t;
        }
        case CommandKind::ToggleBlockQuote: {
            std::size_t ls = find_line_start(text_cps, sel.start.v);
            std::size_t le = find_line_end(text_cps, sel.end.v);
            std::u32string line_text(text_cps.begin() + ls, text_cps.begin() + le);
            std::u32string new_line;
            if (line_text.size() >= 2 && line_text[0] == '>' && line_text[1] == ' ') {
                new_line = std::u32string(line_text.begin() + 2, line_text.end());
            } else {
                new_line.push_back('>'); new_line.push_back(' '); new_line += line_text;
            }
            std::size_t new_pos = ls + new_line.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::StructuralCommand);
            t.with_edit(CharRange(CharOffset(ls), CharOffset(le)), std::move(new_line));
            return t;
        }
        case CommandKind::ToggleUnorderedList:
        case CommandKind::ToggleOrderedList:
        case CommandKind::ToggleTaskList:
            return list_toggle_transaction(cmd, text_cps, selection, revision);
        case CommandKind::InsertCodeBlock: {
            std::u32string block; block.push_back('\n'); block.push_back('`'); block.push_back('`'); block.push_back('`');
            if (cmd.lang) block += *cmd.lang;
            block += U"\n\n```\n";
            std::size_t new_pos = sel.start.v + 4 + (cmd.lang ? cmd.lang->size() : 0);
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::StructuralCommand);
            t.with_edit(sel, std::move(block));
            return t;
        }
        case CommandKind::InsertLink: {
            std::u32string title_part; if (cmd.title) { title_part.push_back(' '); title_part.push_back('"'); title_part += *cmd.title; title_part.push_back('"'); }
            if (sel.is_empty()) {
                std::u32string s; s.push_back('['); s.push_back(']'); s.push_back('('); s += cmd.href; s += title_part; s.push_back(')');
                std::size_t new_pos = sel.start.v + 1;
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(s));
                return t;
            } else {
                auto sel_text = std::u32string(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
                std::u32string s; s.push_back('['); s += sel_text; s.push_back(']'); s.push_back('('); s += cmd.href; s += title_part; s.push_back(')');
                std::size_t new_pos = sel.start.v + s.size();
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(s));
                return t;
            }
        }
        // Commands handled at editor/UI layer — produce no transaction here.
        case CommandKind::Undo: case CommandKind::Redo:
        case CommandKind::Cut: case CommandKind::Copy: case CommandKind::Paste:
        case CommandKind::SelectAll: case CommandKind::ClearHeading:
        case CommandKind::InsertImage: case CommandKind::InsertFootnote:
        case CommandKind::ToggleCallout: case CommandKind::ExtensionCmd:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace elmd

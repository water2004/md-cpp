export module elmd.core.semantic_edit;
import std;
import elmd.core.types;
import elmd.core.selection;
import elmd.core.transaction;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.source_map;
import elmd.core.source_structure;
import elmd.core.utf;
import elmd.core.dialect;
import elmd.core.table_edit;
import elmd.core.command;

export namespace elmd {

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
    if (pos + 5 < line.size() && (line[pos] == U'-' || line[pos] == U'*' || line[pos] == U'+') && line[pos + 1] == U' ' && line[pos + 2] == U'[' && (line[pos + 3] == U' ' || line[pos + 3] == U'x' || line[pos + 3] == U'X') && line[pos + 4] == U']' && line[pos + 5] == U' ') {
        marker.push_back(line[pos]);
        marker += U" [ ] ";
        pos += 6;
    } else if (pos + 1 < line.size() && (line[pos] == U'-' || line[pos] == U'*' || line[pos] == U'+') && line[pos + 1] == U' ') {
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

    if (empty_item && (!marker.empty() || !base_prefix.empty())) return MarkdownNewlineEdit{CharRange(CharOffset(ls), CharOffset(le)), U"", CharOffset(ls)};

    std::u32string insert;
    insert.push_back(U'\n');
    insert += base_prefix;
    insert += marker;
    return MarkdownNewlineEdit{sel, insert, CharOffset(sel.start.v + insert.size())};
}

inline std::size_t markdown_line_prefix_end(const std::u32string& line) {
    std::size_t pos = 0;
    while (pos < line.size() && is_space_or_tab(line[pos])) ++pos;
    if (pos + 5 < line.size() && (line[pos] == U'-' || line[pos] == U'*' || line[pos] == U'+') && line[pos + 1] == U' ' && line[pos + 2] == U'[' && (line[pos + 3] == U' ' || line[pos + 3] == U'x' || line[pos + 3] == U'X') && line[pos + 4] == U']' && line[pos + 5] == U' ') return pos + 6;
    if (pos + 1 < line.size() && (line[pos] == U'-' || line[pos] == U'*' || line[pos] == U'+') && line[pos + 1] == U' ') return pos + 2;
    std::size_t number_start = pos;
    while (pos < line.size() && line[pos] >= U'0' && line[pos] <= U'9') ++pos;
    if (pos > number_start && pos + 1 < line.size() && (line[pos] == U'.' || line[pos] == U')') && line[pos + 1] == U' ') return pos + 2;
    return number_start;
}

inline std::optional<Transaction> list_toggle_transaction(const Command& cmd, const std::u32string& text_cps, const Selection& selection, std::uint64_t revision) {
    auto sel = selection.normalized_range();
    std::size_t ls = find_line_start(text_cps, sel.start.v);
    auto last = sel.end.v;
    if (last > sel.start.v && last > 0 && text_cps[last - 1] == U'\n') --last;
    std::size_t le = find_line_end(text_cps, last);
    struct LineInfo {
        std::u32string line;
        std::u32string indent;
        std::u32string body;
        std::size_t prefix_end = 0;
        bool unordered = false;
        bool ordered = false;
        bool task = false;
    };
    std::vector<LineInfo> lines;
    std::size_t cursor = ls;
    while (cursor <= le) {
        auto end = find_line_end(text_cps, cursor);
        LineInfo info;
        info.line = std::u32string(text_cps.begin() + cursor, text_cps.begin() + end);
        std::size_t indent_end = 0;
        while (indent_end < info.line.size() && is_space_or_tab(info.line[indent_end])) ++indent_end;
        info.prefix_end = markdown_line_prefix_end(info.line);
        info.indent = std::u32string(info.line.begin(), info.line.begin() + indent_end);
        info.body = std::u32string(info.line.begin() + info.prefix_end, info.line.end());
        info.unordered = info.prefix_end == indent_end + 2 && indent_end + 1 < info.line.size() && (info.line[indent_end] == U'-' || info.line[indent_end] == U'*' || info.line[indent_end] == U'+') && info.line[indent_end + 1] == U' ';
        info.task = info.prefix_end == indent_end + 6 && indent_end + 5 < info.line.size() && (info.line[indent_end] == U'-' || info.line[indent_end] == U'*' || info.line[indent_end] == U'+') && info.line[indent_end + 1] == U' ' && info.line[indent_end + 2] == U'[' && info.line[indent_end + 4] == U']' && info.line[indent_end + 5] == U' ';
        info.ordered = info.prefix_end > indent_end + 2 && info.prefix_end <= info.line.size() && (info.line[info.prefix_end - 2] == U'.' || info.line[info.prefix_end - 2] == U')') && info.line[info.prefix_end - 1] == U' ';
        lines.push_back(std::move(info));
        if (end >= le) break;
        cursor = end + 1;
    }
    auto is_target = [&](LineInfo const& line) {
        if (cmd.kind == CommandKind::ToggleOrderedList) return line.ordered;
        if (cmd.kind == CommandKind::ToggleTaskList) return line.task;
        return line.unordered && !line.task;
    };
    auto remove = !lines.empty() && std::all_of(lines.begin(), lines.end(), is_target);
    std::u32string replacement;
    std::size_t ordered_number = 1;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        auto const& line = lines[index];
        replacement += line.indent;
        if (!remove) {
            if (cmd.kind == CommandKind::ToggleOrderedList) replacement += utf8_to_cps(std::to_string(ordered_number++)) + U". ";
            else if (cmd.kind == CommandKind::ToggleTaskList) replacement += U"- [ ] ";
            else replacement += U"- ";
        }
        replacement += line.body;
        if (index + 1 < lines.size()) replacement.push_back(U'\n');
    }
    Selection after;
    if (selection.is_caret() && lines.size() == 1) {
        auto const& line = lines.front();
        auto body_offset = selection.head().v > ls + line.prefix_end ? (std::min)(selection.head().v - ls - line.prefix_end, line.body.size()) : 0;
        auto new_prefix = replacement.size() - line.body.size();
        after = Selection::caret(CharOffset(ls + new_prefix + body_offset));
    } else {
        after = Selection{CharOffset(ls), CharOffset(ls + replacement.size()), selection.affinity};
    }
    Transaction t(revision, selection, after, TransactionReason::StructuralCommand);
    t.with_edit(CharRange(CharOffset(ls), CharOffset(le)), std::move(replacement));
    return t;
}

inline std::optional<Transaction> task_checkbox_transaction(const std::u32string& text_cps, const Selection& selection, std::uint64_t revision) {
    auto sel = selection.normalized_range();
    std::size_t ls = find_line_start(text_cps, sel.start.v);
    std::size_t le = find_line_end(text_cps, sel.start.v);
    std::u32string line(text_cps.begin() + ls, text_cps.begin() + le);
    std::size_t pos = 0;
    while (pos < line.size() && is_space_or_tab(line[pos])) ++pos;
    if (pos + 5 >= line.size() || (line[pos] != U'-' && line[pos] != U'*' && line[pos] != U'+') || line[pos + 1] != U' ' || line[pos + 2] != U'[' || line[pos + 4] != U']' || line[pos + 5] != U' ') return std::nullopt;
    if (line[pos + 3] != U' ' && line[pos + 3] != U'x' && line[pos + 3] != U'X') return std::nullopt;
    auto replacement = line[pos + 3] == U' ' ? U"x" : U" ";
    Transaction t(revision, selection, selection, TransactionReason::StructuralCommand);
    t.with_edit(CharRange(CharOffset(ls + pos + 3), CharOffset(ls + pos + 4)), replacement);
    return t;
}

inline const NodeSourceRange* block_range_at(const MarkdownDocument& document, std::size_t block_index) {
    if (block_index >= document.blocks.size()) return nullptr;
    return document.source_map.find_node_by_id(document.blocks[block_index].id);
}

enum class SemanticEditIntent {
    InsertText,
    DeleteSelection,
    DeletePreviousTextUnit,
    DeleteNextTextUnit,
    InsertParagraphBlockAfter,
    InsertParagraphBlockAfterTable,
    MoveCaretToNextTableRow,
    InsertEmptySiblingBlock,
    InsertCodeLineBreak,
    ContinueContainerLine,
    MoveCaret,
    FormatInline,
    StructuralSourceTransform,
    TableEdit,
    NoEdit,
};

struct SemanticEditPlan {
    SemanticEditIntent intent = SemanticEditIntent::NoEdit;
    std::optional<std::size_t> block_index;
};

inline SemanticEditPlan plan_newline(const std::u32string& text, const MarkdownDocument& document, const Selection& selection) {
    if (!selection.is_caret()) return {SemanticEditIntent::InsertParagraphBlockAfter, std::nullopt};
    auto structure = build_source_structure(document, text);
    if (source_blank_at(structure, selection.head())) return {SemanticEditIntent::InsertEmptySiblingBlock, std::nullopt};
    const auto* semantic = source_semantic_at(structure, selection.head());
    if (!semantic || !semantic->document_block_index) return {SemanticEditIntent::InsertParagraphBlockAfter, std::nullopt};
    auto block_index = *semantic->document_block_index;
    const auto& block = document.blocks[block_index];
    if (block.kind == BlockKind::CodeBlock && semantic->content_range.start <= selection.head() && selection.head() <= semantic->content_range.end) return {SemanticEditIntent::InsertCodeLineBreak, block_index};
    if (block.kind == BlockKind::Table) {
        auto table = table_source_at(text, selection.head().v);
        if (table && !table->rows.empty()) {
            auto position = table_position_at(*table, selection.head().v);
            auto next_row = position.row + 1;
            while (next_row < table->rows.size() && table->rows[next_row].separator) ++next_row;
            if (next_row >= table->rows.size()) return {SemanticEditIntent::InsertParagraphBlockAfterTable, block_index};
            return {SemanticEditIntent::MoveCaretToNextTableRow, block_index};
        }
    }
    if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::List || block.kind == BlockKind::TaskList) return {SemanticEditIntent::ContinueContainerLine, block_index};
    return {SemanticEditIntent::InsertParagraphBlockAfter, block_index};
}

inline std::optional<Transaction> semantic_newline_transaction(const std::u32string& text_cps, const MarkdownDocument& document, const Selection& selection, std::uint64_t revision) {
    auto sel = selection.normalized_range();
    auto caret_after = [&](CharOffset p) { return Selection::caret(p); };
    auto plan = plan_newline(text_cps, document, selection);
    switch (plan.intent) {
        case SemanticEditIntent::InsertCodeLineBreak: {
            Transaction transaction(revision, selection, caret_after(CharOffset(sel.start.v + 1)), TransactionReason::Typing);
            transaction.with_edit(sel, U"\n");
            return transaction;
        }
        case SemanticEditIntent::InsertEmptySiblingBlock: {
            Transaction transaction(revision, selection, caret_after(CharOffset(sel.start.v + 1)), TransactionReason::StructuralCommand);
            transaction.with_edit(sel, U"\n");
            return transaction;
        }
        case SemanticEditIntent::InsertParagraphBlockAfterTable: {
            auto table = table_source_at(text_cps, selection.head().v);
            if (!table || table->rows.empty()) return std::nullopt;
            auto position = table->rows.back().line_range.end.v;
            std::u32string inserted = U"\n\n";
            if (position < text_cps.size() && text_cps[position] == U'\n') {
                ++position;
                inserted = U"\n";
            }
            Transaction transaction(revision, selection, caret_after(CharOffset(position + inserted.size())), TransactionReason::StructuralCommand);
            transaction.with_edit(CharRange(CharOffset(position), CharOffset(position)), inserted);
            return transaction;
        }
        case SemanticEditIntent::MoveCaretToNextTableRow: {
            auto table = table_source_at(text_cps, selection.head().v);
            if (!table || table->rows.empty()) return std::nullopt;
            auto position = table_position_at(*table, selection.head().v);
            auto next_row = position.row + 1;
            while (next_row < table->rows.size() && table->rows[next_row].separator) ++next_row;
            if (next_row >= table->rows.size()) return std::nullopt;
            auto const& row = table->rows[next_row];
            auto target = row.line_range.end.v;
            if (position.column < row.cells.size()) target = row.cells[position.column].content_range.start.v;
            return Transaction(revision, selection, caret_after(CharOffset(target)), TransactionReason::StructuralCommand);
        }
        case SemanticEditIntent::ContinueContainerLine: {
            auto markdown_edit = markdown_newline_edit(text_cps, sel);
            Transaction transaction(revision, selection, caret_after(markdown_edit ? markdown_edit->caret : CharOffset(sel.start.v + 1)), TransactionReason::StructuralCommand);
            if (markdown_edit) transaction.with_edit(markdown_edit->range, markdown_edit->text);
            else transaction.with_edit(sel, U"\n");
            return transaction;
        }
        case SemanticEditIntent::InsertParagraphBlockAfter: {
            std::u32string inserted = U"\n\n";
            if (selection.is_caret() && plan.block_index) {
                if (const auto* range = block_range_at(document, *plan.block_index)) {
                    std::size_t position = selection.head().v;
                    std::size_t newline_run = 0;
                    while (position + newline_run < text_cps.size() && text_cps[position + newline_run] == U'\n') ++newline_run;
                    bool boundary_after = position == range->content_range.end.v
                        && (newline_run >= 2 || (newline_run == 1 && position + newline_run == text_cps.size()));
                    bool boundary_before = position == range->source_range.start.v
                        && position > 0 && text_cps[position - 1] == U'\n';
                    if (boundary_after || boundary_before) inserted = U"\n";
                }
            }
            Transaction transaction(revision, selection, caret_after(CharOffset(sel.start.v + inserted.size())), TransactionReason::StructuralCommand);
            transaction.with_edit(sel, inserted);
            return transaction;
        }
        default:
            return std::nullopt;
    }
}

inline std::optional<Transaction> semantic_transaction(const Command& cmd,
                                                       const std::u32string& text_cps,
                                                       const MarkdownDocument& document,
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
    auto selection_intersects_table = [&]() {
        if (selection.is_caret()) return false;
        for (std::size_t index = 0; index < document.blocks.size(); ++index) {
            if (document.blocks[index].kind != BlockKind::Table) continue;
            auto range = block_range_at(document, index);
            if (range && sel.start < range->source_range.end && range->source_range.start < sel.end) return true;
        }
        return false;
    };

    switch (cmd.kind) {
        case CommandKind::InsertText: {
            std::size_t new_pos = sel.start.v + cmd.text.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::Typing);
            t.with_edit(sel, cmd.text);
            return t;
        }
        case CommandKind::DeleteBackward: {
            if (auto table = table_source_at(text_cps, sel.start.v)) {
                if (!selection.is_caret()) {
                    if (sel.end.v > table->rows.back().line_range.end.v) return std::nullopt;
                    Transaction transaction(revision, selection, caret_after(sel.start), TransactionReason::Delete);
                    for (auto const& row : table->rows) {
                        if (row.separator) continue;
                        for (auto const& cell : row.cells) {
                            auto start = (std::max)(sel.start.v, cell.content_range.start.v);
                            auto end = (std::min)(sel.end.v, cell.content_range.end.v);
                            if (start < end) transaction.with_edit(CharRange(CharOffset(start), CharOffset(end)), U"");
                        }
                    }
                    if (transaction.edits.empty()) return std::nullopt;
                    transaction.selection_after = caret_after(transaction.edits.front().range.start);
                    return transaction;
                }
                auto cell = table_content_cell_at(*table, sel.start.v);
                if (!cell || sel.start.v <= cell->content_range.start.v || sel.start.v > cell->content_range.end.v) return std::nullopt;
                auto previous = (std::max)(prev_grapheme_boundary_char(text_cps, sel.start.v), cell->content_range.start.v);
                Transaction transaction(revision, selection, caret_after(CharOffset(previous)), TransactionReason::Delete);
                transaction.with_edit(CharRange(CharOffset(previous), sel.start), U"");
                return transaction;
            }
            if (!selection.is_caret()) {
                if (selection_intersects_table()) return std::nullopt;
                Transaction t(revision, selection, caret_after(sel.start), TransactionReason::Delete);
                t.with_edit(sel, U"");
                return t;
            }
            std::size_t pos = sel.start.v;
            if (pos == 0) return std::nullopt;
            std::size_t prev = prev_grapheme_boundary_char(text_cps, pos);
            if (table_source_at(text_cps, prev)) return std::nullopt;
            Transaction t(revision, selection, caret_after(CharOffset(prev)), TransactionReason::Delete);
            t.with_edit(CharRange(CharOffset(prev), CharOffset(pos)), U"");
            return t;
        }
        case CommandKind::DeleteForward: {
            if (auto table = table_source_at(text_cps, sel.start.v)) {
                if (!selection.is_caret()) {
                    if (sel.end.v > table->rows.back().line_range.end.v) return std::nullopt;
                    Transaction transaction(revision, selection, caret_after(sel.start), TransactionReason::Delete);
                    for (auto const& row : table->rows) {
                        if (row.separator) continue;
                        for (auto const& cell : row.cells) {
                            auto start = (std::max)(sel.start.v, cell.content_range.start.v);
                            auto end = (std::min)(sel.end.v, cell.content_range.end.v);
                            if (start < end) transaction.with_edit(CharRange(CharOffset(start), CharOffset(end)), U"");
                        }
                    }
                    if (transaction.edits.empty()) return std::nullopt;
                    transaction.selection_after = caret_after(transaction.edits.front().range.start);
                    return transaction;
                }
                auto cell = table_content_cell_at(*table, sel.start.v);
                if (!cell || sel.start.v < cell->content_range.start.v || sel.start.v >= cell->content_range.end.v) return std::nullopt;
                auto next = (std::min)(next_grapheme_boundary_char(text_cps, sel.start.v), cell->content_range.end.v);
                Transaction transaction(revision, selection, caret_after(sel.start), TransactionReason::Delete);
                transaction.with_edit(CharRange(sel.start, CharOffset(next)), U"");
                return transaction;
            }
            if (!selection.is_caret()) {
                if (selection_intersects_table()) return std::nullopt;
                Transaction t(revision, selection, caret_after(sel.start), TransactionReason::Delete);
                t.with_edit(sel, U"");
                return t;
            }
            std::size_t pos = sel.start.v;
            if (pos >= len) return std::nullopt;
            std::size_t nxt = next_grapheme_boundary_char(text_cps, pos);
            if (table_source_at(text_cps, nxt)) return std::nullopt;
            Transaction t(revision, selection, caret_after(CharOffset(pos)), TransactionReason::Delete);
            t.with_edit(CharRange(CharOffset(pos), CharOffset(nxt)), U"");
            return t;
        }
        case CommandKind::DeleteSelection: {
            if (selection.is_caret()) return std::nullopt;
            if (auto table = table_source_at(text_cps, sel.start.v)) {
                if (sel.end.v > table->rows.back().line_range.end.v) return std::nullopt;
                Transaction transaction(revision, selection, caret_after(sel.start), TransactionReason::Delete);
                for (auto const& row : table->rows) {
                    if (row.separator) continue;
                    for (auto const& cell : row.cells) {
                        auto start = (std::max)(sel.start.v, cell.content_range.start.v);
                        auto end = (std::min)(sel.end.v, cell.content_range.end.v);
                        if (start < end) transaction.with_edit(CharRange(CharOffset(start), CharOffset(end)), U"");
                    }
                }
                if (transaction.edits.empty()) return std::nullopt;
                transaction.selection_after = caret_after(transaction.edits.front().range.start);
                return transaction;
            }
            if (selection_intersects_table()) return std::nullopt;
            Transaction t(revision, selection, caret_after(sel.start), TransactionReason::Delete);
            t.with_edit(sel, U"");
            return t;
        }
        case CommandKind::InsertNewline:
            return semantic_newline_transaction(text_cps, document, selection, revision);
        case CommandKind::InsertSoftBreak: {
            Transaction t(revision, selection, caret_after(CharOffset(sel.start.v + 1)), TransactionReason::Typing);
            t.with_edit(sel, U"\n");
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
            return std::nullopt;
        case CommandKind::ToggleStrong:
        case CommandKind::ToggleEmphasis:
        case CommandKind::ToggleStrikethrough:
        case CommandKind::ToggleInlineCode: {
            if (sel.is_empty()) return Transaction(revision, selection, selection, TransactionReason::FormatCommand);
            auto wrap = [&](char32_t a, char32_t b) {
                std::u32string sel_text(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
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
            if (cmd.kind == CommandKind::ToggleEmphasis) {
                std::u32string sel_text(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
                std::u32string wrapped; wrapped.push_back('*'); wrapped += sel_text; wrapped.push_back('*');
                std::size_t new_pos = sel.start.v + wrapped.size();
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(wrapped));
                return t;
            }
            if (cmd.kind == CommandKind::ToggleStrong) return wrap('*', '*');
            if (cmd.kind == CommandKind::ToggleStrikethrough) return wrap('~', '~');
            return wrap('`', '`');
        }
        case CommandKind::InsertMathInline: {
            if (sel.is_empty()) {
                std::u32string tmpl; tmpl.push_back('$'); tmpl.push_back(' '); tmpl.push_back(' '); tmpl.push_back('$');
                std::size_t new_pos = sel.start.v + 2;
                Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
                t.with_edit(sel, std::move(tmpl));
                return t;
            }
            auto sel_text = std::u32string(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
            std::u32string wrapped; wrapped.push_back('$'); wrapped += sel_text; wrapped.push_back('$');
            std::size_t new_pos = sel.start.v + wrapped.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
            t.with_edit(sel, std::move(wrapped));
            return t;
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
        case CommandKind::MoveTableCellNext: return table_edit_transaction(TableEditKind::MoveCellNext, text_cps, selection, revision);
        case CommandKind::MoveTableCellPrevious: return table_edit_transaction(TableEditKind::MoveCellPrevious, text_cps, selection, revision);
        case CommandKind::InsertTableRowAbove: return table_edit_transaction(TableEditKind::InsertRowAbove, text_cps, selection, revision);
        case CommandKind::InsertTableRowBelow: return table_edit_transaction(TableEditKind::InsertRowBelow, text_cps, selection, revision);
        case CommandKind::DeleteTableRow: return table_edit_transaction(TableEditKind::DeleteRow, text_cps, selection, revision);
        case CommandKind::MoveTableRowUp: return table_edit_transaction(TableEditKind::MoveRowUp, text_cps, selection, revision);
        case CommandKind::MoveTableRowDown: return table_edit_transaction(TableEditKind::MoveRowDown, text_cps, selection, revision);
        case CommandKind::InsertTableColumnLeft: return table_edit_transaction(TableEditKind::InsertColumnLeft, text_cps, selection, revision);
        case CommandKind::InsertTableColumnRight: return table_edit_transaction(TableEditKind::InsertColumnRight, text_cps, selection, revision);
        case CommandKind::DeleteTableColumn: return table_edit_transaction(TableEditKind::DeleteColumn, text_cps, selection, revision);
        case CommandKind::MoveTableColumnLeft: return table_edit_transaction(TableEditKind::MoveColumnLeft, text_cps, selection, revision);
        case CommandKind::MoveTableColumnRight: return table_edit_transaction(TableEditKind::MoveColumnRight, text_cps, selection, revision);
        case CommandKind::SetTableColumnAlignment: return table_edit_transaction(TableEditKind::SetColumnAlignment, text_cps, selection, revision, cmd.table_alignment);
        case CommandKind::NormalizeTable: return table_edit_transaction(TableEditKind::Normalize, text_cps, selection, revision);
        case CommandKind::InsertTableRowAt: return table_edit_transaction(TableEditKind::InsertRowAt, text_cps, selection, revision, TableAlignment::None, cmd.table_index);
        case CommandKind::InsertTableColumnAt: return table_edit_transaction(TableEditKind::InsertColumnAt, text_cps, selection, revision, TableAlignment::None, cmd.table_index);
        case CommandKind::MoveTableRowTo: return table_edit_transaction(TableEditKind::MoveRowTo, text_cps, selection, revision, TableAlignment::None, cmd.table_index);
        case CommandKind::MoveTableColumnTo: return table_edit_transaction(TableEditKind::MoveColumnTo, text_cps, selection, revision, TableAlignment::None, cmd.table_index);
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
            std::size_t off = 0; while (off < line_text.size() && line_text[off] == '#') ++off;
            std::u32string stripped(line_text.begin() + off, line_text.end());
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
            if (line_text.size() >= 2 && line_text[0] == '>' && line_text[1] == ' ') new_line = std::u32string(line_text.begin() + 2, line_text.end());
            else { new_line.push_back('>'); new_line.push_back(' '); new_line += line_text; }
            std::size_t new_pos = ls + new_line.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::StructuralCommand);
            t.with_edit(CharRange(CharOffset(ls), CharOffset(le)), std::move(new_line));
            return t;
        }
        case CommandKind::ToggleUnorderedList:
        case CommandKind::ToggleOrderedList:
        case CommandKind::ToggleTaskList:
            return list_toggle_transaction(cmd, text_cps, selection, revision);
        case CommandKind::ToggleTaskCheckbox:
            return task_checkbox_transaction(text_cps, selection, revision);
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
            }
            auto sel_text = std::u32string(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
            std::u32string s; s.push_back('['); s += sel_text; s.push_back(']'); s.push_back('('); s += cmd.href; s += title_part; s.push_back(')');
            std::size_t new_pos = sel.start.v + s.size();
            Transaction t(revision, selection, caret_after(CharOffset(new_pos)), TransactionReason::FormatCommand);
            t.with_edit(sel, std::move(s));
            return t;
        }
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

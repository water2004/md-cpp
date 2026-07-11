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
import elmd.core.auto_pair;
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

inline bool is_space_or_tab(char32_t ch) {
    return ch == U' ' || ch == U'\t';
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

inline const NodeSourceRange* block_range_at(const EditorDocument& document, std::size_t block_index) {
    if (block_index >= document.blocks.size()) return nullptr;
    return document.source_map.find_node_by_id(document.blocks[block_index].id);
}

enum class SemanticEditIntent {
    InsertText,
    DeleteSelection,
    DeletePreviousTextUnit,
    DeleteNextTextUnit,
    InsertParagraphBlockAfter,
    InsertParagraphAfterAtomicBlock,
    InsertParagraphBlockAfterTable,
    MoveCaretToNextTableRow,
    InsertEmptySiblingBlock,
    InsertCodeLineBreak,
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

inline SemanticEditPlan plan_newline(const std::u32string& text, const EditorDocument& document, const Selection& selection) {
    if (!selection.is_caret()) return {SemanticEditIntent::InsertParagraphBlockAfter, std::nullopt};
    auto structure = build_source_structure(document, text);
    if (source_blank_at(structure, selection.head())) return {SemanticEditIntent::InsertEmptySiblingBlock, std::nullopt};
    const auto* semantic = source_semantic_at(structure, selection.head());
    if (!semantic || !semantic->document_block_index) return {SemanticEditIntent::InsertParagraphBlockAfter, std::nullopt};
    auto block_index = *semantic->document_block_index;
    const auto& block = document.blocks[block_index];
    if (block.kind == BlockKind::ThematicBreak) return {SemanticEditIntent::InsertParagraphAfterAtomicBlock, block_index};
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
    if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::List || block.kind == BlockKind::TaskList) return {SemanticEditIntent::NoEdit, block_index};
    return {SemanticEditIntent::InsertParagraphBlockAfter, block_index};
}

inline std::optional<Transaction> semantic_newline_transaction(const std::u32string& text_cps, const EditorDocument& document, const Selection& selection, std::uint64_t revision) {
    auto sel = selection.normalized_range();
    auto caret_after = [&](CharOffset p) { return Selection::caret(p); };
    auto plan = plan_newline(text_cps, document, selection);
    switch (plan.intent) {
        case SemanticEditIntent::InsertCodeLineBreak: {
            if (plan.block_index && document.blocks[*plan.block_index].code_indented && selection.is_caret()) {
                auto line_start = find_line_start(text_cps, sel.start.v);
                auto line_end = find_line_end(text_cps, sel.start.v);
                bool empty_indented_line = sel.start.v == line_end && line_end > line_start;
                for (auto index = line_start; empty_indented_line && index < line_end; ++index) empty_indented_line = is_space_or_tab(text_cps[index]);
                if (empty_indented_line) {
                    Transaction transaction(revision, selection, caret_after(CharOffset(line_start)), TransactionReason::StructuralCommand);
                    transaction.with_edit(CharRange(CharOffset(line_start), CharOffset(line_end)), U"");
                    return transaction;
                }
            }
            auto insertion = std::u32string(U"\n");
            if (plan.block_index && document.blocks[*plan.block_index].code_indented) insertion += U"    ";
            Transaction transaction(revision, selection, caret_after(CharOffset(sel.start.v + insertion.size())), TransactionReason::Typing);
            transaction.with_edit(sel, std::move(insertion));
            return transaction;
        }
        case SemanticEditIntent::InsertEmptySiblingBlock: {
            Transaction transaction(revision, selection, caret_after(CharOffset(sel.start.v + 1)), TransactionReason::StructuralCommand);
            transaction.with_edit(sel, U"\n");
            return transaction;
        }
        case SemanticEditIntent::InsertParagraphAfterAtomicBlock: {
            if (!plan.block_index) return std::nullopt;
            auto range = block_range_at(document, *plan.block_index);
            if (!range) return std::nullopt;
            auto position = range->content_range.end.v;
            if (position < text_cps.size() && text_cps[position] == U'\n') {
                return Transaction(revision, selection, caret_after(CharOffset(position + 1)), TransactionReason::StructuralCommand);
            }
            Transaction transaction(revision, selection, caret_after(CharOffset(position + 1)), TransactionReason::StructuralCommand);
            transaction.with_edit(CharRange(CharOffset(position), CharOffset(position)), U"\n");
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
                                                       const EditorDocument& document,
                                                       const Selection& selection,
                                                       std::uint64_t revision) {
    auto sel = selection.normalized_range();
    std::size_t len = text_cps.size();
    auto caret_after = [&](CharOffset p) { return Selection::caret(p); };
    auto backward_caret = [&](CharOffset p) { auto result = Selection::caret(p); result.affinity = TextAffinity::Upstream; return result; };
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
                Transaction transaction(revision, selection, backward_caret(CharOffset(previous)), TransactionReason::Delete);
                transaction.with_edit(CharRange(CharOffset(previous), sel.start), U"");
                return transaction;
            }
            if (!selection.is_caret()) {
                if (selection_intersects_table()) return std::nullopt;
                Transaction t(revision, selection, caret_after(sel.start), TransactionReason::Delete);
                t.with_edit(sel, U"");
                return t;
            }
            if (auto paired = auto_pair_backspace_transaction(text_cps, selection, revision)) return paired;
            std::size_t pos = sel.start.v;
            if (pos == 0) return std::nullopt;
            std::size_t prev = prev_grapheme_boundary_char(text_cps, pos);
            if (table_source_at(text_cps, prev)) return std::nullopt;
            Transaction t(revision, selection, backward_caret(CharOffset(prev)), TransactionReason::Delete);
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
            if (selection.is_caret()) {
                for (auto const& block : document.blocks) {
                    if (block.kind != BlockKind::ThematicBreak) continue;
                    auto range = document.source_map.find_node_by_id(block.id);
                    if (range && range->content_range.start.v <= sel.start.v && sel.start.v < range->content_range.end.v) {
                        auto target = range->content_range.end;
                        return Transaction(revision, selection, cmd.extend_selection ? extend_after(target) : caret_after(target), TransactionReason::StructuralCommand);
                    }
                }
            }
            std::size_t nxt = std::min(next_grapheme_boundary_char(text_cps, sel.start.v), len);
            return Transaction(revision, selection, cmd.extend_selection ? extend_after(CharOffset(nxt)) : caret_after(CharOffset(nxt)), TransactionReason::Typing);
        }
        case CommandKind::MoveLeft: {
            if (selection.is_caret()) {
                for (auto const& block : document.blocks) {
                    if (block.kind != BlockKind::ThematicBreak) continue;
                    auto range = document.source_map.find_node_by_id(block.id);
                    if (range && range->content_range.start.v < sel.start.v && sel.start.v <= range->content_range.end.v) {
                        auto target = range->content_range.start;
                        return Transaction(revision, selection, cmd.extend_selection ? extend_after(target) : caret_after(target), TransactionReason::StructuralCommand);
                    }
                }
            }
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
        case CommandKind::InsertCodeBlock: {
            std::u32string block; block.push_back('\n'); block.push_back('`'); block.push_back('`'); block.push_back('`');
            if (cmd.lang) block += *cmd.lang;
            block += U"\n\n```\n";
            std::size_t new_pos = sel.start.v + 5 + (cmd.lang ? cmd.lang->size() : 0);
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
        case CommandKind::InsertImage: {
            auto selected = sel.is_empty() ? std::u32string{} : std::u32string(text_cps.begin() + sel.start.v, text_cps.begin() + sel.end.v);
            auto alt = cmd.alt.empty() ? selected : cmd.alt;
            auto path = cmd.path.empty() ? U"image.png" : cmd.path;
            std::u32string replacement = U"![" + alt + U"](" + path + U")";
            auto after = alt.empty()
                ? Selection::caret(CharOffset(sel.start.v + 2))
                : Selection::caret(CharOffset(sel.start.v + replacement.size()));
            Transaction transaction(revision, selection, after, TransactionReason::FormatCommand);
            transaction.with_edit(sel, std::move(replacement));
            return transaction;
        }
        case CommandKind::InsertFootnote: {
            std::u32string label = cmd.text;
            if (label.empty()) {
                std::size_t number = 1;
                do {
                    label = utf8_to_cps(std::to_string(number++));
                } while (text_cps.find(U"[^" + label + U"]:") != std::u32string::npos);
            }
            label.erase(std::remove_if(label.begin(), label.end(), [](char32_t value) {
                return value == U'[' || value == U']' || value == U'^' || value == U'\r' || value == U'\n';
            }), label.end());
            if (label.empty()) label = U"1";
            auto reference = U"[^" + label + U"]";
            std::u32string separator;
            if (!text_cps.empty() && !text_cps.ends_with(U"\n\n")) separator = text_cps.ends_with(U"\n") ? U"\n" : U"\n\n";
            auto definition = separator + U"[^" + label + U"]: ";
            if (sel.end.v == len) {
                auto replacement = reference + definition;
                Transaction transaction(revision, selection, Selection::caret(CharOffset(sel.start.v + replacement.size())), TransactionReason::StructuralCommand);
                transaction.with_edit(sel, std::move(replacement));
                return transaction;
            }
            auto adjustedEnd = len - (sel.end.v - sel.start.v) + reference.size();
            Transaction transaction(revision, selection, Selection::caret(CharOffset(adjustedEnd + definition.size())), TransactionReason::StructuralCommand);
            transaction.with_edit(sel, reference);
            transaction.with_edit(CharRange(CharOffset(len), CharOffset(len)), definition);
            return transaction;
        }
        case CommandKind::ToggleCallout: {
            auto start = find_line_start(text_cps, sel.start.v);
            auto end = find_line_end(text_cps, sel.end.v);
            bool removing = false;
            for (std::size_t index = 0; index < document.blocks.size(); ++index) {
                if (document.blocks[index].kind != BlockKind::Callout) continue;
                auto range = block_range_at(document, index);
                if (range && range->source_range.start.v <= sel.start.v && sel.end.v <= range->source_range.end.v) {
                    start = range->source_range.start.v;
                    end = range->source_range.end.v;
                    if (end > start && text_cps[end - 1] == U'\n') --end;
                    removing = true;
                    break;
                }
            }
            std::vector<std::u32string> lines;
            auto cursor = start;
            while (cursor <= end) {
                auto lineEnd = find_line_end(text_cps, cursor);
                lines.emplace_back(text_cps.begin() + cursor, text_cps.begin() + lineEnd);
                if (lineEnd >= end) break;
                cursor = lineEnd + 1;
            }
            std::u32string replacement;
            if (removing) {
                for (std::size_t index = 1; index < lines.size(); ++index) {
                    auto body = std::size_t{0};
                    while (body < lines[index].size() && is_space_or_tab(lines[index][body])) ++body;
                    if (body < lines[index].size() && lines[index][body] == U'>') ++body;
                    if (body < lines[index].size() && lines[index][body] == U' ') ++body;
                    replacement.append(lines[index].begin() + body, lines[index].end());
                    if (index + 1 < lines.size()) replacement.push_back(U'\n');
                }
            } else {
                auto kind = cmd.callout_kind.empty() ? U"NOTE" : cmd.callout_kind;
                for (auto& value : kind) if (value >= U'a' && value <= U'z') value = static_cast<char32_t>(value - U'a' + U'A');
                kind.erase(std::remove_if(kind.begin(), kind.end(), [](char32_t value) {
                    return !((value >= U'A' && value <= U'Z') || (value >= U'0' && value <= U'9') || value == U'-' || value == U'_');
                }), kind.end());
                if (kind.empty()) kind = U"NOTE";
                replacement = U"> [!" + kind + U"]\n";
                for (std::size_t index = 0; index < lines.size(); ++index) {
                    replacement += U"> ";
                    replacement += lines[index];
                    if (index + 1 < lines.size()) replacement.push_back(U'\n');
                }
            }
            Transaction transaction(revision, selection, Selection::caret(CharOffset(start + replacement.size())), TransactionReason::StructuralCommand);
            transaction.with_edit(CharRange(CharOffset(start), CharOffset(end)), std::move(replacement));
            return transaction;
        }
        case CommandKind::Undo: case CommandKind::Redo:
        case CommandKind::Cut: case CommandKind::Copy: case CommandKind::Paste:
        case CommandKind::SelectAll: case CommandKind::ExtensionCmd:
        case CommandKind::IndentListItem: case CommandKind::OutdentListItem:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace elmd

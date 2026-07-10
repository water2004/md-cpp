export module elmd.core.table_edit;
import std;
import elmd.core.types;
import elmd.core.dialect;
import elmd.core.selection;
import elmd.core.transaction;

export namespace elmd {

enum class TableEditKind {
    MoveCellNext,
    MoveCellPrevious,
    InsertRowAbove,
    InsertRowBelow,
    DeleteRow,
    MoveRowUp,
    MoveRowDown,
    InsertColumnLeft,
    InsertColumnRight,
    DeleteColumn,
    MoveColumnLeft,
    MoveColumnRight,
    SetColumnAlignment,
    Normalize,
    InsertRowAt,
    InsertColumnAt,
    MoveRowTo,
    MoveColumnTo,
};

struct TableCellSource {
    CharRange content_range;
    std::u32string text;
};

struct TableRowSource {
    CharRange line_range;
    std::vector<TableCellSource> cells;
    bool separator = false;
};

struct TableSource {
    CharRange range;
    std::vector<TableRowSource> rows;
    std::size_t column_count = 0;
    bool trailing_newline = false;
};

struct TablePosition {
    std::size_t row = 0;
    std::size_t column = 0;
};

inline bool table_is_space(char32_t ch) {
    return ch == U' ' || ch == U'\t';
}

inline std::size_t table_line_start(std::u32string_view text, std::size_t pos) {
    pos = (std::min)(pos, text.size());
    while (pos > 0 && text[pos - 1] != U'\n') --pos;
    return pos;
}

inline std::size_t table_line_end(std::u32string_view text, std::size_t pos) {
    pos = (std::min)(pos, text.size());
    while (pos < text.size() && text[pos] != U'\n') ++pos;
    return pos;
}

inline std::u32string table_trim(std::u32string_view text) {
    std::size_t start = 0;
    while (start < text.size() && table_is_space(text[start])) ++start;
    std::size_t end = text.size();
    while (end > start && table_is_space(text[end - 1])) --end;
    return std::u32string(text.substr(start, end - start));
}

inline bool table_separator_cell(std::u32string_view text) {
    auto trimmed = table_trim(text);
    if (trimmed.empty()) return false;
    bool has_dash = false;
    for (auto ch : trimmed) {
        if (ch == U'-') has_dash = true;
        else if (ch != U':') return false;
    }
    return has_dash;
}

inline std::optional<TableRowSource> parse_table_source_row(std::u32string_view text, std::size_t line_start, std::size_t line_end) {
    if (line_start >= line_end) return std::nullopt;
    auto line = text.substr(line_start, line_end - line_start);
    if (line.find(U'|') == std::u32string_view::npos) return std::nullopt;

    std::vector<std::pair<std::size_t, std::size_t>> segments;
    std::size_t segment_start = line.empty() || line.front() != U'|' ? 0 : 1;
    for (std::size_t index = segment_start; index < line.size(); ++index) {
        if (line[index] == U'\\' && index + 1 < line.size()) {
            ++index;
            continue;
        }
        if (line[index] != U'|') continue;
        segments.push_back({ segment_start, index });
        segment_start = index + 1;
    }
    if (segment_start < line.size()) segments.push_back({ segment_start, line.size() });
    if (segments.empty()) return std::nullopt;

    TableRowSource row;
    row.line_range = CharRange(CharOffset(line_start), CharOffset(line_end));
    bool separator = true;
    for (auto const& segment : segments) {
        auto start = segment.first;
        auto end = segment.second;
        while (start < end && table_is_space(line[start])) ++start;
        while (end > start && table_is_space(line[end - 1])) --end;
        if (start == segment.second) {
            start = segment.first;
            if (start < segment.second && table_is_space(line[start])) ++start;
            end = start;
        }
        TableCellSource cell;
        cell.content_range = CharRange(CharOffset(line_start + start), CharOffset(line_start + end));
        cell.text = std::u32string(line.substr(start, end - start));
        separator = separator && table_separator_cell(line.substr(segment.first, segment.second - segment.first));
        row.cells.push_back(std::move(cell));
    }
    row.separator = separator;
    return row;
}

inline const TableCellSource* table_content_cell_at(TableSource const& table, std::size_t offset) {
    for (auto const& row : table.rows) {
        if (row.separator) continue;
        for (auto const& cell : row.cells) {
            if (cell.content_range.start.v <= offset && offset <= cell.content_range.end.v) return &cell;
        }
    }
    return nullptr;
}

inline bool table_candidate_row(std::u32string_view text, std::size_t line_start) {
    auto line_end = table_line_end(text, line_start);
    return parse_table_source_row(text, line_start, line_end).has_value();
}

inline std::optional<TableSource> table_source_at(std::u32string_view text, std::size_t offset) {
    if (text.empty()) return std::nullopt;
    offset = (std::min)(offset, text.size());
    auto current = table_line_start(text, offset);
    auto start = current;
    while (start > 0) {
        auto previous_end = start - 1;
        auto previous_start = table_line_start(text, previous_end);
        if (!table_candidate_row(text, previous_start)) break;
        start = previous_start;
    }

    std::vector<TableRowSource> rows;
    auto cursor = start;
    while (cursor < text.size()) {
        auto line_end = table_line_end(text, cursor);
        auto row = parse_table_source_row(text, cursor, line_end);
        if (!row) break;
        rows.push_back(std::move(*row));
        cursor = line_end < text.size() && text[line_end] == U'\n' ? line_end + 1 : line_end;
        if (cursor == line_end) break;
    }
    if (rows.size() < 2 || !rows[1].separator) return std::nullopt;
    if (offset < rows.front().line_range.start.v || offset > rows.back().line_range.end.v) return std::nullopt;

    TableSource table;
    table.range = CharRange(rows.front().line_range.start, CharOffset(cursor));
    table.trailing_newline = table.range.end.v > table.range.start.v && table.range.end.v <= text.size() && text[table.range.end.v - 1] == U'\n';
    table.rows = std::move(rows);
    for (auto const& row : table.rows) table.column_count = (std::max)(table.column_count, row.cells.size());
    if (table.column_count == 0) return std::nullopt;
    return table;
}

inline TablePosition table_position_at(TableSource const& table, std::size_t offset) {
    TablePosition position;
    for (std::size_t row_index = 0; row_index < table.rows.size(); ++row_index) {
        auto const& row = table.rows[row_index];
        if (row.line_range.start.v <= offset && offset <= row.line_range.end.v) {
            position.row = row.separator && row_index + 1 < table.rows.size() ? row_index + 1 : row_index;
            auto const& active_row = table.rows[position.row];
            for (std::size_t column = 0; column < active_row.cells.size(); ++column) {
                auto const& cell = active_row.cells[column];
                if (offset <= cell.content_range.end.v) {
                    position.column = column;
                    return position;
                }
            }
            position.column = active_row.cells.empty() ? 0 : active_row.cells.size() - 1;
            return position;
        }
    }
    position.row = table.rows.size() - 1;
    position.column = 0;
    return position;
}

inline std::vector<std::vector<std::u32string>> table_matrix(TableSource const& table) {
    std::vector<std::vector<std::u32string>> matrix;
    matrix.reserve(table.rows.size() - 1);
    for (std::size_t row_index = 0; row_index < table.rows.size(); ++row_index) {
        if (table.rows[row_index].separator) continue;
        std::vector<std::u32string> row(table.column_count);
        for (std::size_t column = 0; column < table.rows[row_index].cells.size() && column < table.column_count; ++column) {
            row[column] = table.rows[row_index].cells[column].text;
        }
        matrix.push_back(std::move(row));
    }
    return matrix;
}

inline TableAlignment table_alignment_from_separator_cell(std::u32string_view text) {
    auto marker = table_trim(text);
    bool left = !marker.empty() && marker.front() == U':';
    bool right = !marker.empty() && marker.back() == U':';
    if (left && right) return TableAlignment::Center;
    if (left) return TableAlignment::Left;
    if (right) return TableAlignment::Right;
    return TableAlignment::None;
}

inline std::vector<TableAlignment> table_alignments(TableSource const& table) {
    std::vector<TableAlignment> alignments(table.column_count, TableAlignment::None);
    if (table.rows.size() < 2) return alignments;
    for (std::size_t column = 0; column < table.rows[1].cells.size() && column < alignments.size(); ++column) {
        alignments[column] = table_alignment_from_separator_cell(table.rows[1].cells[column].text);
    }
    return alignments;
}

inline std::u32string format_table_matrix(std::vector<std::vector<std::u32string>> const& matrix, std::vector<TableAlignment> alignments, bool trailing_newline) {
    if (matrix.empty()) return {};
    std::size_t column_count = 0;
    for (auto const& row : matrix) column_count = (std::max)(column_count, row.size());
    alignments.resize(column_count, TableAlignment::None);
    std::vector<std::size_t> widths(column_count, 3);
    for (auto const& row : matrix) {
        for (std::size_t column = 0; column < row.size(); ++column) widths[column] = (std::max)(widths[column], row[column].size());
    }

    auto append_row = [&](std::u32string& out, std::vector<std::u32string> const& row) {
        out.push_back(U'|');
        for (std::size_t column = 0; column < column_count; ++column) {
            out.push_back(U' ');
            if (column < row.size()) out += row[column];
            auto length = column < row.size() ? row[column].size() : 0;
            while (length++ < widths[column]) out.push_back(U' ');
            out.push_back(U' ');
            out.push_back(U'|');
        }
        out.push_back(U'\n');
    };

    std::u32string out;
    append_row(out, matrix.front());
    out.push_back(U'|');
    for (std::size_t column = 0; column < column_count; ++column) {
        out.push_back(U' ');
        std::u32string marker(widths[column], U'-');
        if (alignments[column] == TableAlignment::Left || alignments[column] == TableAlignment::Center) marker.front() = U':';
        if (alignments[column] == TableAlignment::Right || alignments[column] == TableAlignment::Center) marker.back() = U':';
        out += marker;
        out.push_back(U' ');
        out.push_back(U'|');
    }
    out.push_back(U'\n');
    for (std::size_t row = 1; row < matrix.size(); ++row) append_row(out, matrix[row]);
    if (!trailing_newline && !out.empty()) out.pop_back();
    return out;
}

inline std::size_t formatted_cell_offset(std::vector<std::vector<std::u32string>> const& matrix, std::size_t row, std::size_t column) {
    if (matrix.empty()) return 0;
    std::size_t column_count = 0;
    for (auto const& source_row : matrix) column_count = (std::max)(column_count, source_row.size());
    std::vector<std::size_t> widths(column_count, 3);
    for (auto const& source_row : matrix) {
        for (std::size_t source_column = 0; source_column < source_row.size(); ++source_column) widths[source_column] = (std::max)(widths[source_column], source_row[source_column].size());
    }
    row = (std::min)(row, matrix.size() - 1);
    column = (std::min)(column, column_count == 0 ? 0 : column_count - 1);
    std::size_t offset = 0;
    for (std::size_t row_index = 0; row_index < row; ++row_index) {
        for (auto width : widths) offset += width + 3;
        offset += 2;
        if (row_index == 0) {
            for (auto width : widths) offset += width + 3;
            offset += 2;
        }
    }
    offset += 2;
    for (std::size_t column_index = 0; column_index < column; ++column_index) offset += widths[column_index] + 3;
    return offset;
}

inline std::optional<Transaction> table_edit_transaction(TableEditKind kind, std::u32string_view text, Selection selection, std::uint64_t revision, TableAlignment requested_alignment = TableAlignment::None, std::size_t requested_index = 0) {
    auto table = table_source_at(text, selection.head().v);
    if (!table) return std::nullopt;
    auto position = table_position_at(*table, selection.head().v);
    auto matrix = table_matrix(*table);
    if (matrix.empty()) return std::nullopt;
    auto alignments = table_alignments(*table);
    position.row = position.row == 0 ? 0 : position.row - 1;
    position.row = (std::min)(position.row, matrix.size() - 1);
    position.column = (std::min)(position.column, table->column_count - 1);

    auto target_row = position.row;
    auto target_column = position.column;
    bool changed = true;
    switch (kind) {
        case TableEditKind::MoveCellNext:
            changed = false;
            if (target_column + 1 < table->column_count) ++target_column;
            else if (target_row + 1 < matrix.size()) { ++target_row; target_column = 0; }
            break;
        case TableEditKind::MoveCellPrevious:
            changed = false;
            if (target_column > 0) --target_column;
            else if (target_row > 0) { --target_row; target_column = table->column_count - 1; }
            break;
        case TableEditKind::InsertRowAbove:
            matrix.insert(matrix.begin() + target_row, std::vector<std::u32string>(table->column_count));
            break;
        case TableEditKind::InsertRowBelow:
            matrix.insert(matrix.begin() + target_row + 1, std::vector<std::u32string>(table->column_count));
            ++target_row;
            break;
        case TableEditKind::DeleteRow:
            if (matrix.size() <= 1) return std::nullopt;
            matrix.erase(matrix.begin() + target_row);
            target_row = (std::min)(target_row, matrix.size() - 1);
            break;
        case TableEditKind::MoveRowUp:
            if (target_row == 0) return std::nullopt;
            std::swap(matrix[target_row], matrix[target_row - 1]);
            --target_row;
            break;
        case TableEditKind::MoveRowDown:
            if (target_row + 1 >= matrix.size()) return std::nullopt;
            std::swap(matrix[target_row], matrix[target_row + 1]);
            ++target_row;
            break;
        case TableEditKind::InsertColumnLeft:
            for (auto& row : matrix) row.insert(row.begin() + target_column, U"");
            alignments.insert(alignments.begin() + target_column, TableAlignment::None);
            break;
        case TableEditKind::InsertColumnRight:
            for (auto& row : matrix) row.insert(row.begin() + target_column + 1, U"");
            alignments.insert(alignments.begin() + target_column + 1, TableAlignment::None);
            ++target_column;
            break;
        case TableEditKind::DeleteColumn:
            if (table->column_count <= 1) return std::nullopt;
            for (auto& row : matrix) row.erase(row.begin() + target_column);
            alignments.erase(alignments.begin() + target_column);
            target_column = (std::min)(target_column, table->column_count - 2);
            break;
        case TableEditKind::MoveColumnLeft:
            if (target_column == 0) return std::nullopt;
            for (auto& row : matrix) std::swap(row[target_column], row[target_column - 1]);
            std::swap(alignments[target_column], alignments[target_column - 1]);
            --target_column;
            break;
        case TableEditKind::MoveColumnRight:
            if (target_column + 1 >= table->column_count) return std::nullopt;
            for (auto& row : matrix) std::swap(row[target_column], row[target_column + 1]);
            std::swap(alignments[target_column], alignments[target_column + 1]);
            ++target_column;
            break;
        case TableEditKind::SetColumnAlignment:
            alignments[target_column] = requested_alignment;
            break;
        case TableEditKind::Normalize:
            break;
        case TableEditKind::InsertRowAt:
            target_row = (std::min)(requested_index, matrix.size());
            matrix.insert(matrix.begin() + target_row, std::vector<std::u32string>(table->column_count));
            break;
        case TableEditKind::InsertColumnAt:
            target_column = (std::min)(requested_index, table->column_count);
            for (auto& row : matrix) row.insert(row.begin() + target_column, U"");
            alignments.insert(alignments.begin() + target_column, TableAlignment::None);
            break;
        case TableEditKind::MoveRowTo: {
            auto insertion = (std::min)(requested_index, matrix.size());
            if (insertion == target_row || insertion == target_row + 1) return std::nullopt;
            auto moved = std::move(matrix[target_row]);
            matrix.erase(matrix.begin() + target_row);
            if (insertion > target_row) --insertion;
            insertion = (std::min)(insertion, matrix.size());
            matrix.insert(matrix.begin() + insertion, std::move(moved));
            target_row = insertion;
            break;
        }
        case TableEditKind::MoveColumnTo: {
            auto insertion = (std::min)(requested_index, table->column_count);
            if (insertion == target_column || insertion == target_column + 1) return std::nullopt;
            for (auto& row : matrix) {
                auto moved = std::move(row[target_column]);
                row.erase(row.begin() + target_column);
                auto adjusted = insertion > target_column ? insertion - 1 : insertion;
                adjusted = (std::min)(adjusted, row.size());
                row.insert(row.begin() + adjusted, std::move(moved));
            }
            auto moved_alignment = alignments[target_column];
            alignments.erase(alignments.begin() + target_column);
            if (insertion > target_column) --insertion;
            insertion = (std::min)(insertion, alignments.size());
            alignments.insert(alignments.begin() + insertion, moved_alignment);
            target_column = insertion;
            break;
        }
    }

    auto replacement = changed ? format_table_matrix(matrix, alignments, table->trailing_newline) : std::u32string(text.substr(table->range.start.v, table->range.len()));
    auto caret = table->range.start.v;
    if (!changed) {
        auto source_row = target_row == 0 ? 0 : target_row + 1;
        if (source_row < table->rows.size() && target_column < table->rows[source_row].cells.size()) {
            caret = table->rows[source_row].cells[target_column].content_range.start.v;
        }
    }
    else if (auto formatted = table_source_at(replacement, 0)) {
        auto source_row = target_row == 0 ? 0 : target_row + 1;
        if (source_row < formatted->rows.size() && target_column < formatted->rows[source_row].cells.size()) {
            caret += formatted->rows[source_row].cells[target_column].content_range.start.v;
        }
    }
    Transaction transaction(revision, selection, Selection::caret(CharOffset(caret)), changed ? TransactionReason::StructuralCommand : TransactionReason::Typing);
    if (changed) transaction.with_edit(table->range, std::move(replacement));
    return transaction;
}

}

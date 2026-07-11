export module elmd.core.auto_pair;
import std;
import elmd.core.types;
import elmd.core.selection;
import elmd.core.transaction;

export namespace elmd {

inline bool pairable_marker(char32_t value) {
    return value == U'*' || value == U'_' || value == U'$' || value == U'`';
}

inline std::optional<Transaction> auto_pair_insert_transaction(
    std::u32string_view text,
    Selection selection,
    std::uint64_t revision,
    std::u32string_view inserted)
{
    if (!selection.is_caret()) return std::nullopt;
    auto position = selection.active.v;
    if (position > text.size() || inserted.size() != 1) return std::nullopt;
    auto marker = inserted.front();
    auto caret_after = [&](std::size_t value) { return Selection::caret(CharOffset(value)); };

    if (marker == U'~') {
        if (position > 0 && text[position - 1] == U'~' && (position >= text.size() || text[position] != U'~')) {
            Transaction transaction(revision, selection, caret_after(position + 1), TransactionReason::Typing);
            transaction.with_edit(CharRange(CharOffset(position - 1), CharOffset(position)), U"~~~~");
            return transaction;
        }
        return std::nullopt;
    }

    if (!pairable_marker(marker)) return std::nullopt;
    std::size_t left_count = 0;
    while (left_count < position && text[position - left_count - 1] == marker) ++left_count;
    std::size_t right_count = 0;
    while (position + right_count < text.size() && text[position + right_count] == marker) ++right_count;

    if (right_count > 0) {
        bool closing_run = left_count == 0;
        auto cursor = position - left_count;
        bool has_content = false;
        while (cursor > 0 && text[cursor - 1] != marker) {
            has_content = true;
            --cursor;
        }
        if (has_content && cursor > 0 && text[cursor - 1] == marker) closing_run = true;
        if (closing_run) return Transaction(revision, selection, caret_after(position + 1), TransactionReason::Typing);
    }

    if (left_count > 0 && left_count == right_count) {
        if (marker == U'$' && left_count >= 2) return std::nullopt;
        if (marker == U'`' && left_count == 2) {
            auto line_start = position - left_count;
            while (line_start > 0 && text[line_start - 1] != U'\n') --line_start;
            auto marker_start = position - left_count;
            auto marker_end = position + right_count;
            bool only_indent = true;
            for (auto index = line_start; index < marker_start; ++index) {
                if (text[index] != U' ' && text[index] != U'\t') only_indent = false;
            }
            auto line_end = marker_end;
            while (line_end < text.size() && text[line_end] != U'\n') ++line_end;
            bool only_suffix = true;
            for (auto index = marker_end; index < line_end; ++index) {
                if (text[index] != U' ' && text[index] != U'\t') only_suffix = false;
            }
            if (only_indent && only_suffix) {
                Transaction transaction(revision, selection, caret_after(marker_start + 4), TransactionReason::Typing);
                transaction.with_edit(CharRange(CharOffset(marker_start), CharOffset(marker_end)), U"```\n\n```");
                return transaction;
            }
        }
        Transaction transaction(revision, selection, caret_after(position + 1), TransactionReason::Typing);
        transaction.with_edit(CharRange(CharOffset(position), CharOffset(position)), std::u32string(2, marker));
        return transaction;
    }

    Transaction transaction(revision, selection, caret_after(position + 1), TransactionReason::Typing);
    transaction.with_edit(CharRange(CharOffset(position), CharOffset(position)), std::u32string(2, marker));
    return transaction;
}

inline std::optional<Transaction> auto_pair_backspace_transaction(
    std::u32string_view text,
    Selection selection,
    std::uint64_t revision)
{
    if (!selection.is_caret()) return std::nullopt;
    auto position = selection.active.v;
    if (position == 0 || position >= text.size()) return std::nullopt;
    auto marker = text[position - 1];
    if ((!pairable_marker(marker) && marker != U'~') || text[position] != marker) return std::nullopt;
    Transaction transaction(revision, selection, Selection::caret(CharOffset(position - 1)), TransactionReason::Delete);
    transaction.with_edit(CharRange(CharOffset(position - 1), CharOffset(position + 1)), U"");
    return transaction;
}

}

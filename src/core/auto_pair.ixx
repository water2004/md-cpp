export module elmd.core.auto_pair;
import std;
import elmd.core.types;
import elmd.core.selection;
import elmd.core.transaction;

export namespace elmd {

inline bool pairable_marker(char32_t value) {
    return value == U'*' || value == U'_' || value == U'$' || value == U'`';
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

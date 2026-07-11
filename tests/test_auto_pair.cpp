import std;
#include "test_framework.h"
import elmd.core.types;
import elmd.core.selection;
import elmd.core.transaction;
import elmd.core.auto_pair;

using namespace elmd;

static std::u32string apply_pair_transaction(std::u32string source, Transaction const& transaction) {
    for (auto iterator = transaction.edits.rbegin(); iterator != transaction.edits.rend(); ++iterator) {
        source.replace(iterator->range.start.v, iterator->range.end.v - iterator->range.start.v, iterator->replacement);
    }
    return source;
}

ELMD_TEST(auto_pair_inserts_inline_delimiters) {
    for (auto marker : std::u32string(U"*_`$")) {
        auto transaction = auto_pair_insert_transaction(U"", Selection::caret(CharOffset(0)), 1, std::u32string(1, marker));
        ELMD_CHECK(transaction.has_value());
        ELMD_CHECK_EQ(apply_pair_transaction(U"", *transaction), std::u32string(2, marker));
        ELMD_CHECK_EQ(transaction->selection_after.active.v, 1u);
    }
}

ELMD_TEST(auto_pair_expands_emphasis_and_math_runs) {
    auto strong = auto_pair_insert_transaction(U"**", Selection::caret(CharOffset(1)), 1, U"*");
    ELMD_CHECK(strong.has_value());
    ELMD_CHECK_EQ(apply_pair_transaction(U"**", *strong), U"****");
    ELMD_CHECK_EQ(strong->selection_after.active.v, 2u);
    auto display_math = auto_pair_insert_transaction(U"$$", Selection::caret(CharOffset(1)), 1, U"$");
    ELMD_CHECK(display_math.has_value());
    ELMD_CHECK_EQ(apply_pair_transaction(U"$$", *display_math), U"$$$$");
}

ELMD_TEST(auto_pair_builds_strike_after_second_tilde) {
    auto transaction = auto_pair_insert_transaction(U"~", Selection::caret(CharOffset(1)), 1, U"~");
    ELMD_CHECK(transaction.has_value());
    ELMD_CHECK_EQ(apply_pair_transaction(U"~", *transaction), U"~~~~");
    ELMD_CHECK_EQ(transaction->selection_after.active.v, 2u);
}

ELMD_TEST(auto_pair_promotes_three_backticks_to_fence) {
    auto transaction = auto_pair_insert_transaction(U"````", Selection::caret(CharOffset(2)), 1, U"`");
    ELMD_CHECK(transaction.has_value());
    ELMD_CHECK_EQ(apply_pair_transaction(U"````", *transaction), U"```\n\n```");
    ELMD_CHECK_EQ(transaction->selection_after.active.v, 4u);
}

ELMD_TEST(auto_pair_skips_closer_and_deletes_empty_pair) {
    auto closer = auto_pair_insert_transaction(U"*value*", Selection::caret(CharOffset(6)), 1, U"*");
    ELMD_CHECK(closer.has_value());
    ELMD_CHECK(closer->edits.empty());
    ELMD_CHECK_EQ(closer->selection_after.active.v, 7u);
    auto strong_first = auto_pair_insert_transaction(U"**value**", Selection::caret(CharOffset(7)), 1, U"*");
    ELMD_CHECK(strong_first.has_value());
    ELMD_CHECK_EQ(strong_first->selection_after.active.v, 8u);
    auto strong_second = auto_pair_insert_transaction(U"**value**", strong_first->selection_after, 1, U"*");
    ELMD_CHECK(strong_second.has_value());
    ELMD_CHECK(strong_second->edits.empty());
    ELMD_CHECK_EQ(strong_second->selection_after.active.v, 9u);
    auto deletion = auto_pair_backspace_transaction(U"**", Selection::caret(CharOffset(1)), 1);
    ELMD_CHECK(deletion.has_value());
    ELMD_CHECK_EQ(apply_pair_transaction(U"**", *deletion), U"");
}

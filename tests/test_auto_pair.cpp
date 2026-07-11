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

ELMD_TEST(auto_pair_backspace_deletes_empty_source_pair) {
    auto deletion = auto_pair_backspace_transaction(U"**", Selection::caret(CharOffset(1)), 1);
    ELMD_CHECK(deletion.has_value());
    ELMD_CHECK_EQ(apply_pair_transaction(U"**", *deletion), U"");
}

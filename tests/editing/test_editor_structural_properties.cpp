#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.document;
import folia.core.block_tree;
import folia.core.document_edit;
import folia.core.document_text;
import folia.core.editor;
import folia.core.instrumentation;
import folia.core.serializer;
import folia.core.text_edit;
import folia.core.utf;

using namespace folia;
using namespace boost::ut;

namespace {

std::string recursive_context(std::size_t context, std::string_view source) {
    const auto text = std::string(source);
    switch (context) {
        case 0: return text;
        case 1: return "# " + text;
        case 2: return "- " + text;
        case 3: return "- [ ] " + text;
        case 4: return "> " + text;
        case 5: return "| " + text + " |\n| --- |";
        case 6: return "> [!NOTE] " + text + "\n> body";
        case 7: return "> [!NOTE] title\n> " + text;
        case 8: return "[^n]: " + text;
        case 9: return "- > " + text;
        case 10: return "> - " + text;
        case 11: return "- [ ] > " + text;
        case 12: return "[^n]: > " + text;
        default: return "- > [!NOTE] title\n  > " + text;
    }
}

std::optional<DocumentTextFragment> source_owner(const EditorDocument& document, std::u32string_view source) {
    auto fragments = document_text_fragments(document);
    auto found = std::ranges::find_if(fragments, [&](const auto& fragment) {
        return fragment.text == source;
    });
    if (found == fragments.end()) return std::nullopt;
    return *found;
}

void expect_no_global_edit_work(CoreOperationCounters counters, std::size_t label) {
    expect(fatal(bool(counters.full_document_parses == 0u))) << label << " full parse";
    expect(fatal(bool(counters.full_document_serializations == 0u))) << label << " full serialize";
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << label << " full tree diff";
}

} // namespace

suite editor_structural_property_tests = [] {

"enter_then_semantic_merge_is_exact_across_incomplete_inline_states_and_recursive_containers"_test = [] {
    const std::vector<std::u32string> samples{
        U"abc", U"*abc*", U"_abc_", U"**abc**", U"__abc__", U"**", U"**abc",
        U"a***b***c", U"~~abc~~", U"~~abc", U"`abc`", U"`abc", U"[title](url)",
        U"[title](<url>)", U"[title](url \"name\")", U"[title](", U"![alt](url)",
        U"$abc$", U"$abc", U"\\*abc\\*", U"&amp;", U"a\\**b*",
    };

    constexpr std::size_t context_count = 14;
    for (std::size_t sample_index = 0; sample_index < samples.size(); ++sample_index) {
        const auto original = U"p" + samples[sample_index] + U"q";
        for (std::size_t context = 0; context < context_count; ++context) {
            const auto label = sample_index * context_count + context;
            Editor editor(recursive_context(context, cps_to_utf8(original)));
            const auto owner = source_owner(editor.document(), original);
            expect(fatal(bool(owner.has_value()))) << label << " owner";
            if (!owner) continue;

            const auto offset = 1 + samples[sample_index].size() / 2;
            const auto before_selection = TextSelection::caret({
                owner->container_id,
                offset,
                TextAffinity::Downstream,
            });
            editor.set_selection(before_selection);
            const auto before_markdown = editor.markdown_utf8();

            reset_core_operation_counters();
            const auto entered = editor.execute_document_enter(editor.selection());
            expect(fatal(bool(entered.has_value()))) << label << " enter";
            if (!entered) continue;
            expect_no_global_edit_work(read_core_operation_counters(), label);
            expect(fatal(bool(validate_document(editor.document()).empty()))) << label << " enter invariant";

            const auto entered_markdown = editor.markdown_utf8();
            const auto entered_selection = editor.selection();
            Editor reloaded(entered_markdown);
            expect(fatal(bool(reloaded.markdown_utf8() == entered_markdown))) << label << " reload";
            expect(fatal(bool(validate_document(reloaded.document()).empty()))) << label << " reload invariant";

            expect(fatal(editor.undo())) << label << " enter undo";
            expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << label << " enter undo source";
            expect(fatal(bool(editor.selection() == before_selection))) << label << " enter undo selection";
            expect(fatal(editor.redo())) << label << " enter redo";
            expect(fatal(bool(editor.markdown_utf8() == entered_markdown))) << label << " enter redo source";
            expect(fatal(bool(editor.selection() == entered_selection))) << label << " enter redo selection";

            reset_core_operation_counters();
            const auto* entered_parent = find_parent_block(
                editor.document().root, entered_selection.active.container_id);
            std::optional<DocumentTransaction> joined;
            if (entered_parent && (entered_parent->kind == BlockKind::ListItem
                    || entered_parent->kind == BlockKind::TaskListItem)) {
                const auto left = original.substr(0, offset);
                const auto left_owner = source_owner(editor.document(), left);
                expect(fatal(bool(left_owner.has_value()))) << label << " split list head";
                if (!left_owner) continue;
                editor.set_selection(TextSelection::caret({
                    left_owner->container_id,
                    left.size(),
                    TextAffinity::Downstream,
                }));
                editor.set_selection(TextSelection{
                    editor.selection().active,
                    entered_selection.active,
                });
                joined = editor.execute_document_delete_selection(editor.selection());
            } else {
                joined = editor.execute_document_delete_backward(editor.selection());
            }
            const auto merge_selection = joined ? joined->selection_before : editor.selection();
            expect(fatal(bool(joined.has_value()))) << label << " semantic merge";
            if (!joined) continue;
            expect_no_global_edit_work(read_core_operation_counters(), label);
            expect(fatal(bool(editor.markdown_utf8() == before_markdown)))
                << label << " semantic inverse: " << editor.markdown_utf8()
                << " != " << before_markdown;
            expect(fatal(bool(validate_document(editor.document()).empty()))) << label << " join invariant";

            const auto joined_selection = editor.selection();
            expect(fatal(editor.undo())) << label << " join undo";
            expect(fatal(bool(editor.markdown_utf8() == entered_markdown))) << label << " join undo source";
            expect(fatal(bool(editor.selection() == merge_selection))) << label << " join undo selection";
            expect(fatal(editor.redo())) << label << " join redo";
            expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << label << " join redo source";
            expect(fatal(bool(editor.selection() == joined_selection))) << label << " join redo selection";
        }
    }
};

}; // suite editor_structural_property_tests

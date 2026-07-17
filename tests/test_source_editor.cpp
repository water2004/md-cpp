#include <algorithm>
#include <random>
#include <string>

#include "elmd_test.hpp"
import elmd.core.source_editor;
import elmd.core.source_render;
import elmd.core.source_style;

using namespace elmd;
using namespace boost::ut;

suite source_editor_tests = [] {

"source edits are exact flat text edits with reversible selections"_test = [] {
    SourceEditor editor(U"# title\n\ntext");
    editor.set_selection(SourceSelection::caret(2));
    expect(fatal(editor.insert_text(U"X")));
    expect(fatal(bool(editor.source() == U"# Xtitle\n\ntext")));
    expect(fatal(bool(editor.selection() == SourceSelection::caret(3))));
    expect(fatal(editor.undo()));
    expect(fatal(bool(editor.source() == U"# title\n\ntext")));
    expect(fatal(bool(editor.selection() == SourceSelection::caret(2))));
    expect(fatal(editor.redo()));
    expect(fatal(bool(editor.source() == U"# Xtitle\n\ntext")));
    expect(fatal(bool(editor.selection() == SourceSelection::caret(3))));
};

"source line projection preserves untouched identities and exact characters"_test = [] {
    SourceEditor editor(U"first\nsecond\nthird");
    auto first = editor.lines()[0].id;
    auto second = editor.lines()[1].id;
    auto third = editor.lines()[2].id;
    editor.set_selection(SourceSelection::caret(8));
    expect(fatal(editor.insert_text(U"X")));
    expect(fatal(bool(editor.lines().size() == 3u)));
    expect(fatal(bool(editor.lines()[0].id == first)));
    expect(fatal(bool(editor.lines()[1].id == second)));
    expect(fatal(bool(editor.lines()[2].id == third)));
    auto model = build_source_render_model(editor);
    std::u32string visible;
    for (std::size_t index = 0; index < model.blocks.size(); ++index) {
        if (index) visible.push_back(U'\n');
        for (auto const& item : model.blocks[index].inline_items) visible += item.text;
    }
    expect(fatal(bool(visible == editor.source())));
};

"source styling keeps every markdown marker visible"_test = [] {
    SourceEditor editor(U"# **bold** and $x$\n\n```cpp\nreturn 1;\n```\n");
    auto model = build_source_render_model(editor);
    std::u32string visible;
    for (std::size_t index = 0; index < model.blocks.size(); ++index) {
        if (index) visible.push_back(U'\n');
        for (auto const& item : model.blocks[index].inline_items) visible += item.text;
    }
    expect(fatal(bool(visible == editor.source())));
    expect(fatal(bool(editor.lines().size() == 6u)));
    expect(fatal(bool(editor.lines()[3].code_content)));
    expect(fatal(bool(editor.lines()[3].code_language == std::optional<std::string>{"cpp"})));
    auto has = [&](std::size_t line, SourceSyntaxKind kind) {
        return std::ranges::any_of(editor.lines()[line].styles, [&](auto const& span) { return span.kind == kind; });
    };
    expect(fatal(has(0, SourceSyntaxKind::Marker)));
    expect(fatal(has(0, SourceSyntaxKind::Strong)));
    expect(fatal(has(0, SourceSyntaxKind::Math)));
    expect(fatal(has(2, SourceSyntaxKind::Marker)));
    expect(fatal(has(3, SourceSyntaxKind::Code)));
};

"source fence state only invalidates lines until lexical state converges"_test = [] {
    SourceEditor editor(U"before\n```cpp\nint x;\n```\nafter");
    auto after_id = editor.lines().back().id;
    auto after_key = editor.lines().back().presentation_key;
    editor.set_selection(SourceSelection::caret(0));
    expect(fatal(editor.insert_text(U"X")));
    expect(fatal(bool(editor.lines().back().id == after_id)));
    expect(fatal(bool(editor.lines().back().presentation_key == after_key)));
};

"source line repair reports only the converged edited interval"_test = [] {
    std::u32string source;
    for (std::size_t index = 0; index < 4096; ++index) {
        source += U"line ";
        for (auto digit : std::to_string(index)) source.push_back(static_cast<char32_t>(digit));
        if (index + 1 != 4096) source.push_back(U'\n');
    }
    SourceEditor editor(std::move(source));
    auto before_id = editor.lines()[2047].id;
    auto edited_id = editor.lines()[2048].id;
    auto after_id = editor.lines()[2049].id;
    editor.set_selection(SourceSelection::caret(editor.lines()[2048].source_start + 2));
    expect(fatal(editor.insert_text(U"X")));
    auto const& change = editor.last_line_change();
    expect(fatal(bool(change.old_start == 2048u)));
    expect(fatal(bool(change.old_end == 2049u)));
    expect(fatal(bool(change.new_start == 2048u)));
    expect(fatal(bool(change.new_end == 2049u)));
    expect(fatal(bool(change.old_line_count == 4096u)));
    expect(fatal(bool(change.new_line_count == 4096u)));
    expect(fatal(bool(editor.lines()[2047].id == before_id)));
    expect(fatal(bool(editor.lines()[2048].id == edited_id)));
    expect(fatal(bool(editor.lines()[2049].id == after_id)));
};

"source line repair follows fence state only to its convergence point"_test = [] {
    SourceEditor editor(U"plain\nbody\ntail");
    editor.set_selection(SourceSelection::caret(0));
    expect(fatal(editor.insert_text(U"```\ninside\n```\n")));
    auto const& change = editor.last_line_change();
    expect(fatal(bool(change.new_start == 0u)));
    expect(fatal(bool(change.new_end == 4u)));
    expect(fatal(bool(editor.lines()[1].code_content)));
    expect(fatal(bool(!editor.lines()[2].code_content)));
    expect(fatal(bool(!editor.lines()[3].code_content)));
};

"source code presentation invalidates one fenced context rather than the document"_test = [] {
    SourceEditor editor(U"before\n```cpp\n/* first\nstill comment */\n```\nafter");
    auto before = build_source_render_model(editor);
    auto firstCodeKey = before.blocks[2].presentation_key;
    auto secondCodeKey = before.blocks[3].presentation_key;
    auto afterKey = before.blocks.back().presentation_key;
    editor.set_selection(SourceSelection::caret(editor.lines()[2].source_start + 2));
    expect(fatal(editor.insert_text(U"X")));
    auto after = build_source_render_model_incremental(editor, std::move(before));
    expect(fatal(bool(after.blocks[2].presentation_key != firstCodeKey)));
    expect(fatal(bool(after.blocks[3].presentation_key != secondCodeKey)));
    expect(fatal(bool(after.blocks.back().presentation_key == afterKey)));
    expect(fatal(bool(after.blocks[2].source_code_context == after.blocks[3].source_code_context)));
    expect(fatal(after.incremental_update));
    expect(fatal(bool(after.changed_block_indices == std::vector<std::size_t>{2u, 3u})));
};

"source render projection updates one ordinary line in place"_test = [] {
    std::u32string source;
    for (std::size_t index = 0; index < 4096; ++index) {
        source += U"line";
        if (index + 1 != 4096) source.push_back(U'\n');
    }
    SourceEditor editor(std::move(source));
    auto model = build_source_render_model(editor);
    auto untouchedKey = model.blocks[2049].presentation_key;
    editor.set_selection(SourceSelection::caret(editor.lines()[2048].source_start + 2));
    expect(fatal(editor.insert_text(U"X")));
    model = build_source_render_model_incremental(editor, std::move(model));
    expect(fatal(model.incremental_update));
    expect(fatal(bool(model.rebuilt_block_count == 1u)));
    expect(fatal(bool(model.reused_block_count == 4095u)));
    expect(fatal(bool(model.changed_block_indices == std::vector<std::size_t>{2048u})));
    expect(fatal(bool(model.blocks[2049].presentation_key == untouchedKey)));
};

"source render projection exposes logical line numbers and renumbers after splits"_test = [] {
    SourceEditor editor(U"one\ntwo\nthree");
    auto model = build_source_render_model(editor);
    expect(fatal(bool(model.blocks[0].source_line_number == 1u)));
    expect(fatal(bool(model.blocks[1].source_line_number == 2u)));
    expect(fatal(bool(model.blocks[2].source_line_number == 3u)));

    editor.set_selection(SourceSelection::caret(1));
    expect(fatal(editor.insert_newline()));
    model = build_source_render_model_incremental(editor, std::move(model));
    expect(fatal(bool(model.blocks.size() == 4u)));
    for (std::size_t index = 0; index < model.blocks.size(); ++index)
        expect(fatal(bool(model.blocks[index].source_line_number == index + 1)));
};

"source outline recognizes visible headings without parsing the full document"_test = [] {
    SourceEditor editor(
        U"# **First**\n"
        U"> ## Nested\n"
        U"- ### Listed\n"
        U"Setext *title*\n"
        U"---\n"
        U"```md\n"
        U"# not a heading\n"
        U"```\n");
    auto model = build_source_render_model(editor);
    auto flat = model.outline.flat_items();
    expect(fatal(bool(flat.size() == 4u)));
    expect(fatal(bool(flat[0]->title_plain_text == "First" && flat[0]->level == 1u)));
    expect(fatal(bool(flat[1]->title_plain_text == "Nested" && flat[1]->level == 2u)));
    expect(fatal(bool(flat[2]->title_plain_text == "Listed" && flat[2]->level == 3u)));
    expect(fatal(bool(flat[3]->title_plain_text == "Setext title" && flat[3]->level == 2u)));
    expect(fatal(bool(flat[0]->position.container_id == editor.lines()[0].id)));
};

"source outline repairs only the edited heading neighborhood"_test = [] {
    SourceEditor editor(U"# First\nbody\n## Last");
    auto model = build_source_render_model(editor);
    auto first_id = model.outline.flat_items()[0]->id;
    auto last_id = model.outline.flat_items()[1]->id;

    editor.set_selection({editor.lines()[1].source_start, editor.lines()[1].source_end()});
    expect(fatal(editor.replace_selection(U"### Middle")));
    model = build_source_render_model_incremental(editor, std::move(model));
    auto flat = model.outline.flat_items();
    expect(fatal(bool(flat.size() == 3u)));
    expect(fatal(bool(flat[0]->id == first_id && flat[0]->title_plain_text == "First")));
    expect(fatal(bool(flat[1]->title_plain_text == "Middle" && flat[1]->level == 3u)));
    expect(fatal(bool(flat[2]->id == last_id && flat[2]->title_plain_text == "Last")));
};

"source outline repairs the preceding line when a setext marker changes"_test = [] {
    SourceEditor editor(U"Title\nbody\n# Tail");
    auto model = build_source_render_model(editor);
    expect(fatal(bool(model.outline.flat_items().size() == 1u)));

    editor.set_selection({editor.lines()[1].source_start, editor.lines()[1].source_end()});
    expect(fatal(editor.replace_selection(U"===")));
    model = build_source_render_model_incremental(editor, std::move(model));
    auto flat = model.outline.flat_items();
    expect(fatal(bool(flat.size() == 2u)));
    expect(fatal(bool(flat[0]->title_plain_text == "Title" && flat[0]->level == 1u)));

    editor.set_selection({editor.lines()[1].source_start, editor.lines()[1].source_end()});
    expect(fatal(editor.replace_selection(U"body")));
    model = build_source_render_model_incremental(editor, std::move(model));
    flat = model.outline.flat_items();
    expect(fatal(bool(flat.size() == 1u && flat[0]->title_plain_text == "Tail")));
};

"source positions translate only at the render boundary"_test = [] {
    SourceEditor editor(U"one\ntwo");
    editor.set_selection({1, 6, TextAffinity::Downstream, TextAffinity::Upstream});
    auto projected = editor.projected_selection();
    expect(fatal(bool(projected.anchor.container_id == editor.lines()[0].id)));
    expect(fatal(bool(projected.anchor.source_offset == 1u)));
    expect(fatal(bool(projected.active.container_id == editor.lines()[1].id)));
    expect(fatal(bool(projected.active.source_offset == 2u)));
    expect(fatal(bool(editor.source_offset_from_position(projected.active) == 6u)));
};

"source horizontal movement collapses selections at their visual edge"_test = [] {
    SourceEditor editor(U"abcdef");
    editor.set_selection({5, 2});
    editor.move_left();
    expect(fatal(bool(editor.selection() == SourceSelection::caret(2))));
    editor.set_selection({2, 5});
    editor.move_right();
    expect(fatal(bool(editor.selection() == SourceSelection::caret(5))));
};

"source line indentation is one reversible selection-preserving transaction"_test = [] {
    SourceEditor editor(U"one\ntwo\nthree");
    editor.set_selection({1, 7, TextAffinity::Downstream, TextAffinity::Upstream});
    expect(fatal(editor.indent()));
    expect(fatal(bool(editor.source() == U"    one\n    two\nthree")));
    expect(fatal(bool(editor.selection().anchor == 5u)));
    expect(fatal(bool(editor.selection().active == 15u)));
    expect(fatal(editor.undo()));
    expect(fatal(bool(editor.source() == U"one\ntwo\nthree")));
    expect(fatal(bool(editor.selection().anchor == 1u)));
    expect(fatal(bool(editor.selection().active == 7u)));
    expect(fatal(editor.redo()));
    expect(fatal(editor.outdent()));
    expect(fatal(bool(editor.source() == U"one\ntwo\nthree")));
    expect(fatal(bool(editor.selection().anchor == 1u)));
    expect(fatal(bool(editor.selection().active == 7u)));
};

"source caret tab inserts text while shift tab keeps its column"_test = [] {
    SourceEditor editor(U"    value");
    editor.set_selection(SourceSelection::caret(7));
    expect(fatal(editor.outdent()));
    expect(fatal(bool(editor.source() == U"value")));
    expect(fatal(bool(editor.selection() == SourceSelection::caret(3))));
    expect(fatal(editor.indent()));
    expect(fatal(bool(editor.source() == U"val    ue")));
    expect(fatal(bool(editor.selection() == SourceSelection::caret(7))));
};

"source lines preserve mixed physical line endings"_test = [] {
    const std::u32string source = U"# first\r\n\r\n## second\rthird\n";
    SourceEditor editor(source);
    expect(fatal(bool(editor.lines().size() == 5u)));
    const std::array<std::u32string, 5> texts{U"# first", U"", U"## second", U"third", U""};
    const std::array<std::u32string, 5> endings{U"\r\n", U"\r\n", U"\r", U"\n", U""};
    std::u32string reconstructed;
    for (std::size_t index = 0; index < editor.lines().size(); ++index) {
        expect(fatal(bool(editor.lines()[index].text == texts[index]))) << index;
        expect(fatal(bool(editor.lines()[index].line_ending == endings[index]))) << index;
        reconstructed += editor.lines()[index].text;
        reconstructed += editor.lines()[index].line_ending;
    }
    expect(fatal(bool(reconstructed == source)));

    const auto model = build_source_render_model(editor);
    expect(fatal(bool(model.blocks.size() == 5u)));
    for (std::size_t index = 0; index < model.blocks.size(); ++index)
        expect(fatal(bool(model.blocks[index].source_line_number == index + 1u))) << index;

    const auto downstream_inside_crlf = editor.position_from_source_offset(8u, TextAffinity::Downstream);
    expect(fatal(bool(downstream_inside_crlf.container_id == editor.lines()[1].id)));
    expect(fatal(bool(downstream_inside_crlf.source_offset == 0u)));
    const auto upstream_inside_crlf = editor.position_from_source_offset(8u, TextAffinity::Upstream);
    expect(fatal(bool(upstream_inside_crlf.container_id == editor.lines()[0].id)));
    expect(fatal(bool(upstream_inside_crlf.source_offset == editor.lines()[0].text.size())));

    editor.set_selection(SourceSelection::caret(7u));
    editor.move_right();
    expect(fatal(bool(editor.selection().active == 9u))) << "right crosses CRLF atomically";
    editor.move_left();
    expect(fatal(bool(editor.selection().active == 7u))) << "left crosses CRLF atomically";
    expect(fatal(editor.delete_forward()));
    expect(fatal(bool(editor.source() == U"# first\r\n## second\rthird\n")));
    expect(fatal(editor.undo()));
    expect(fatal(bool(editor.source() == source)));
};

"random source edits preserve the flat source and render projection"_test = [] {
    SourceEditor editor(U"# start\n\ntext");
    auto initial = editor.source();
    auto model = build_source_render_model(editor);
    std::mt19937 random(0x5eedu);
    std::u32string alphabet = U"ab*_`$\\&#=>-[]() 012\U0001f642";
    for (std::size_t step = 0; step < 300; ++step) {
        auto previous_revision = editor.revision();
        switch (random() % 7) {
            case 0: editor.insert_text(std::u32string(1, alphabet[random() % alphabet.size()])); break;
            case 1: editor.insert_newline(); break;
            case 2: editor.delete_backward(); break;
            case 3: editor.delete_forward(); break;
            case 4: editor.move_left(false); break;
            case 5: editor.move_right(false); break;
            case 6: {
                auto first = static_cast<std::size_t>(random() % (editor.source().size() + 1));
                auto second = static_cast<std::size_t>(random() % (editor.source().size() + 1));
                editor.set_selection({first, second});
                break;
            }
        }
        if (editor.revision() != previous_revision)
            model = build_source_render_model_incremental(editor, std::move(model));
        auto selection = editor.selection();
        expect(fatal(selection.anchor <= editor.source().size()));
        expect(fatal(selection.active <= editor.source().size()));
        std::u32string fromLines;
        for (auto const& line : editor.lines()) {
            fromLines += line.text;
            fromLines += line.line_ending;
        }
        expect(fatal(bool(fromLines == editor.source())));
        std::u32string visible;
        for (std::size_t index = 0; index < model.blocks.size(); ++index) {
            if (index) visible.push_back(U'\n');
            for (auto const& item : model.blocks[index].inline_items) visible += item.text;
        }
        expect(fatal(bool(visible == editor.source())));
        auto projected = editor.projected_selection();
        expect(fatal(bool(editor.source_offset_from_position(projected.anchor) == selection.anchor)));
        expect(fatal(bool(editor.source_offset_from_position(projected.active) == selection.active)));

        auto rebuilt = build_source_render_model(editor);
        expect(fatal(bool(model.outline.content_key == rebuilt.outline.content_key))) << step;
        auto incremental_headings = model.outline.flat_items();
        auto rebuilt_headings = rebuilt.outline.flat_items();
        expect(fatal(bool(incremental_headings.size() == rebuilt_headings.size()))) << step;
        for (std::size_t index = 0; index < incremental_headings.size(); ++index) {
            expect(fatal(bool(incremental_headings[index]->id == rebuilt_headings[index]->id))) << step;
            expect(fatal(bool(incremental_headings[index]->level == rebuilt_headings[index]->level))) << step;
            expect(fatal(bool(incremental_headings[index]->title_plain_text
                == rebuilt_headings[index]->title_plain_text))) << step;
            expect(fatal(bool(incremental_headings[index]->slug == rebuilt_headings[index]->slug))) << step;
        }
    }
    while (editor.has_undo()) expect(fatal(editor.undo()));
    expect(fatal(bool(editor.source() == initial)));
};

};

#include <algorithm>
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

};

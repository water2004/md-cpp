#include <string>
#include <string_view>

#include "elmd_test.hpp"
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document_interaction;
import elmd.core.editor;
import elmd.core.instrumentation;
import elmd.core.text_edit;

using namespace elmd;
using namespace boost::ut;

namespace {

const BlockNode* first_block(const Editor& editor, BlockKind kind) {
    const BlockNode* result = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& block) {
        if (!result && block.kind == kind) result = &block;
    });
    return result;
}

const BlockNode* last_block(const Editor& editor, BlockKind kind) {
    const BlockNode* result = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& block) {
        if (block.kind == kind) result = &block;
    });
    return result;
}

} // namespace

suite document_interaction_tests = [] {

"link interaction is resolved from one inline owner"_test = [] {
    Editor editor("prefix [label](https://example.com \"Link title\") suffix");
    const auto* paragraph = first_block(editor, BlockKind::Paragraph);
    expect(fatal(paragraph != nullptr));
    auto interaction = document_interaction_at(
        editor.document(),
        {paragraph->id, 10, TextAffinity::Downstream});
    expect(fatal(interaction.has_value()));
    expect(interaction->link == std::optional<std::string>{"https://example.com"});
    expect(interaction->tooltip == std::optional<std::string>{"Link title"});
};

"nested image keeps its parent link and own tooltip"_test = [] {
    Editor editor("prefix [![image alt](asset.png)](https://example.com) suffix");
    const auto* paragraph = first_block(editor, BlockKind::Paragraph);
    expect(fatal(paragraph != nullptr));
    auto interaction = document_interaction_at(
        editor.document(),
        {paragraph->id, 12, TextAffinity::Downstream});
    expect(fatal(interaction.has_value()));
    expect(interaction->link == std::optional<std::string>{"https://example.com"});
    expect(interaction->tooltip == std::optional<std::string>{"image alt"});
};

"html anchor interaction is resolved from its inline cst"_test = [] {
    Editor editor("prefix <a href=\"https://example.com/html\" title=\"HTML title\">label</a> suffix");
    const auto* paragraph = first_block(editor, BlockKind::Paragraph);
    expect(fatal(paragraph != nullptr));
    auto const labelOffset = paragraph->inline_content.source.find(U"label");
    expect(fatal(labelOffset != std::u32string::npos));
    auto interaction = document_interaction_at(
        editor.document(),
        {paragraph->id, labelOffset + 2, TextAffinity::Downstream});
    expect(fatal(interaction.has_value()));
    expect(interaction->link == std::optional<std::string>{"https://example.com/html"});
    expect(interaction->tooltip == std::optional<std::string>{"HTML title"});
};

"html image keeps its anchor link and image tooltip"_test = [] {
    Editor editor("prefix <a href=\"https://example.com/badge\"><img src=\"badge.svg\" alt=\"badge alt\"></a> suffix");
    const auto* paragraph = first_block(editor, BlockKind::Paragraph);
    expect(fatal(paragraph != nullptr));
    auto const imageOffset = paragraph->inline_content.source.find(U"<img");
    expect(fatal(imageOffset != std::u32string::npos));
    auto interaction = document_interaction_at(
        editor.document(),
        {paragraph->id, imageOffset + 2, TextAffinity::Downstream});
    expect(fatal(interaction.has_value()));
    expect(interaction->link == std::optional<std::string>{"https://example.com/badge"});
    expect(interaction->tooltip == std::optional<std::string>{"badge alt"});
};

"block image interaction is local"_test = [] {
    Editor editor("[![image alt](asset.png \"Image title\")](https://example.com)\n");
    const auto* image = first_block(editor, BlockKind::ImageBlock);
    expect(fatal(image != nullptr));
    auto interaction = document_interaction_at(
        editor.document(),
        {image->id, 0, TextAffinity::Downstream});
    expect(fatal(interaction.has_value()));
    expect(interaction->link == std::optional<std::string>{"https://example.com"});
    expect(interaction->tooltip == std::optional<std::string>{"Image title"});
};

"interaction lookup stays block-local in a large document"_test = [] {
    std::string source;
    source.reserve(160'000);
    for (std::size_t index = 0; index < 2'000; ++index) {
        source += "paragraph ";
        source += std::to_string(index);
        source += " with [link](https://example.com/";
        source += std::to_string(index);
        source += ")\n\n";
    }
    source.resize(source.size() - 2);
    Editor editor(std::move(source));
    const auto* paragraph = last_block(editor, BlockKind::Paragraph);
    expect(fatal(paragraph != nullptr));
    auto const linkOffset = paragraph->inline_content.source.find(U"[link]");
    expect(fatal(linkOffset != std::u32string::npos));
    reset_core_operation_counters();
    auto interaction = document_interaction_at(
        editor.document(),
        {paragraph->id, linkOffset + 2, TextAffinity::Downstream});
    expect(fatal(interaction.has_value()));
    expect(interaction->link.has_value());
    expect(read_core_operation_counters().full_document_block_index_scans == 0_u);
};

};

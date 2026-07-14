// elmd.core.source_render — exact-character render projection for source mode.
export module elmd.core.source_render;
import std;
import elmd.core.source_editor;
import elmd.core.source_style;
import elmd.core.render_model;
import elmd.core.outline;

export namespace elmd {

namespace source_render_detail {

struct CombinedStyle {
    InlineStyle inline_style;
    SourceSyntaxKind syntax = SourceSyntaxKind::None;
};

inline void apply(CombinedStyle& style, SourceSyntaxKind kind) {
    switch (kind) {
        case SourceSyntaxKind::Strong: style.inline_style.bold = true; break;
        case SourceSyntaxKind::Emphasis: style.inline_style.italic = true; break;
        case SourceSyntaxKind::Strikethrough: style.inline_style.strikethrough = true; break;
        case SourceSyntaxKind::Link: style.inline_style.link = true; break;
        case SourceSyntaxKind::Heading: style.inline_style.bold = true; break;
        default: break;
    }
    if (kind != SourceSyntaxKind::None) style.syntax = kind;
}

inline std::vector<InlineRenderItem> line_items(SourceLine const& line) {
    std::vector<std::size_t> boundaries{0, line.text.size()};
    for (auto const& span : line.styles) {
        boundaries.push_back((std::min)(span.range.start, line.text.size()));
        boundaries.push_back((std::min)(span.range.end, line.text.size()));
    }
    std::ranges::sort(boundaries);
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    std::vector<InlineRenderItem> items;
    for (std::size_t index = 1; index < boundaries.size(); ++index) {
        auto start = boundaries[index - 1];
        auto end = boundaries[index];
        if (start == end) continue;
        CombinedStyle combined;
        for (auto const& span : line.styles) {
            if (span.range.start <= start && span.range.end >= end) apply(combined, span.kind);
        }
        InlineRenderItem item;
        item.kind = InlineRenderItem::Kind::Text;
        item.source_span = {line.id, {start, end}};
        item.source_text = line.text.substr(start, end - start);
        item.text = item.source_text;
        item.style = combined.inline_style;
        item.source_syntax = combined.syntax;
        item.visibility = MarkerVisibility::Always;
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace source_render_detail

inline RenderModel build_source_render_model(SourceEditor const& editor) {
    RenderModel model;
    model.revision = editor.revision();
    model.outline = Outline::empty(editor.revision());
    model.blocks.reserve(editor.lines().size());
    model.editable_order.reserve(editor.lines().size());
    for (auto const& line : editor.lines()) {
        RenderBlock block;
        block.kind = RenderBlockKind::Text;
        block.id = line.id;
        block.source_span = {line.id, {0, line.text.size()}};
        block.content_span = block.source_span;
        block.inline_items = source_render_detail::line_items(line);
        block.block_style = {};
        block.source_mode = true;
        block.source_code = line.code_content;
        block.presentation_key = line.presentation_key;
        block.language = line.code_language;
        model.editable_order.push_back(line.id);
        model.blocks.push_back(std::move(block));
    }
    return model;
}

} // namespace elmd

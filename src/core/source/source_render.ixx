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
    struct CodeContext {
        std::shared_ptr<const std::u32string> text;
        std::size_t line_offset = 0;
        std::uint64_t key = 0;
    };
    std::vector<std::optional<CodeContext>> code_contexts(editor.lines().size());
    auto hash_context = [](std::u32string_view text) {
        std::uint64_t value = 1469598103934665603ull;
        for (auto character : text) {
            value ^= static_cast<std::uint32_t>(character);
            value *= 1099511628211ull;
        }
        return value;
    };
    for (std::size_t start = 0; start < editor.lines().size();) {
        if (!editor.lines()[start].code_content) { ++start; continue; }
        auto end = start;
        std::u32string context;
        std::vector<std::size_t> offsets;
        while (end < editor.lines().size() && editor.lines()[end].code_content) {
            offsets.push_back(context.size());
            context += editor.lines()[end].text;
            if (editor.lines()[end].has_newline) context.push_back(U'\n');
            ++end;
        }
        auto key = hash_context(context);
        auto shared = std::make_shared<const std::u32string>(std::move(context));
        for (auto index = start; index < end; ++index) {
            code_contexts[index] = CodeContext{shared, offsets[index - start], key};
        }
        start = end;
    }

    for (std::size_t index = 0; index < editor.lines().size(); ++index) {
        auto const& line = editor.lines()[index];
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
        if (code_contexts[index]) {
            block.source_code_context = code_contexts[index]->text;
            block.source_code_context_offset = code_contexts[index]->line_offset;
            block.presentation_key ^= code_contexts[index]->key
                + 0x9e3779b97f4a7c15ull
                + (block.presentation_key << 6)
                + (block.presentation_key >> 2);
        }
        block.language = line.code_language;
        model.editable_index.emplace(line.id.v, model.editable_order.size());
        model.editable_order.push_back(line.id);
        model.blocks.push_back(std::move(block));
    }
    model.rebuilt_block_count = model.blocks.size();
    return model;
}

} // namespace elmd

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
        item.text = line.text.substr(start, end - start);
        item.style = combined.inline_style;
        item.source_syntax = combined.syntax;
        item.ensure_special().visibility = MarkerVisibility::Always;
        items.push_back(std::move(item));
    }
    return items;
}

struct CodeContext {
    std::shared_ptr<const std::u32string> text;
    std::size_t line_offset = 0;
    std::uint64_t key = 0;
};

inline std::uint64_t hash_context(std::u32string_view text) {
    std::uint64_t value = 1469598103934665603ull;
    for (auto character : text) {
        value ^= static_cast<std::uint32_t>(character);
        value *= 1099511628211ull;
    }
    return value;
}

inline std::vector<std::optional<CodeContext>> code_contexts(
    std::vector<SourceLine> const& lines,
    std::size_t start,
    std::size_t end) {
    std::vector<std::optional<CodeContext>> contexts(end - start);
    for (auto run_start = start; run_start < end;) {
        if (!lines[run_start].code_content) { ++run_start; continue; }
        auto run_end = run_start;
        std::u32string context;
        std::vector<std::size_t> offsets;
        while (run_end < end && lines[run_end].code_content) {
            offsets.push_back(context.size());
            context += lines[run_end].text;
            if (lines[run_end].has_newline) context.push_back(U'\n');
            ++run_end;
        }
        auto key = hash_context(context);
        auto shared = std::make_shared<const std::u32string>(std::move(context));
        for (auto index = run_start; index < run_end; ++index)
            contexts[index - start] = CodeContext{shared, offsets[index - run_start], key};
        run_start = run_end;
    }
    return contexts;
}

inline RenderBlock render_line(
    SourceLine const& line,
    CodeContext const* code_context) {
    RenderBlock block;
    block.kind = RenderBlockKind::Text;
    block.id = line.id;
    block.source_span = {line.id, {0, line.text.size()}};
    block.content_span = block.source_span;
    block.inline_items = line_items(line);
    block.block_style = {};
    block.source_mode = true;
    block.source_code = line.code_content;
    block.presentation_key = line.presentation_key;
    if (code_context) {
        block.source_code_context = code_context->text;
        block.source_code_context_offset = code_context->line_offset;
        block.presentation_key ^= code_context->key
            + 0x9e3779b97f4a7c15ull
            + (block.presentation_key << 6)
            + (block.presentation_key >> 2);
    }
    block.ensure_special().language = line.code_language;
    return block;
}

inline void append_editable_line(RenderModel& model, SourceLine const& line) {
    model.editable_index.emplace(line.id.v, model.editable_order.size());
    model.editable_top_level.emplace(line.id.v, line.id);
    model.editable_order.push_back(line.id);
}

} // namespace source_render_detail

inline RenderModel build_source_render_model(SourceEditor const& editor) {
    RenderModel model;
    model.revision = editor.revision();
    model.outline = Outline::empty(editor.revision());
    model.blocks.reserve(editor.lines().size());
    model.editable_order.reserve(editor.lines().size());
    auto code_contexts = source_render_detail::code_contexts(
        editor.lines(), 0, editor.lines().size());

    for (std::size_t index = 0; index < editor.lines().size(); ++index) {
        auto const& line = editor.lines()[index];
        model.blocks.push_back(source_render_detail::render_line(
            line,
            code_contexts[index] ? &*code_contexts[index] : nullptr));
        source_render_detail::append_editable_line(model, line);
    }
    model.rebuilt_block_count = model.blocks.size();
    return model;
}

inline RenderModel build_source_render_model_incremental(
    SourceEditor const& editor,
    RenderModel previous) {
    auto const& lines = editor.lines();
    auto const& change = editor.last_line_change();
    if (previous.blocks.size() != change.old_line_count
        || (!previous.blocks.empty() && !previous.blocks.front().source_mode)
        || lines.size() != change.new_line_count
        || change.new_start > change.new_end
        || change.new_end > lines.size())
        return build_source_render_model(editor);

    auto rebuild_start = change.new_start;
    auto rebuild_end = change.new_end;
    while (rebuild_start > 0 && lines[rebuild_start - 1].code_content) --rebuild_start;
    while (rebuild_end < lines.size() && lines[rebuild_end].code_content) ++rebuild_end;
    auto contexts = source_render_detail::code_contexts(lines, rebuild_start, rebuild_end);
    auto rebuild = [&](std::size_t index) {
        auto const& context = contexts[index - rebuild_start];
        return source_render_detail::render_line(
            lines[index],
            context ? &*context : nullptr);
    };

    if (previous.blocks.size() == lines.size()) {
        for (auto index = rebuild_start; index < rebuild_end; ++index) {
            auto old_id = previous.blocks[index].id;
            previous.blocks[index] = rebuild(index);
            if (old_id != lines[index].id) {
                previous.editable_index.erase(old_id.v);
                previous.editable_top_level.erase(old_id.v);
                previous.editable_index[lines[index].id.v] = index;
                previous.editable_top_level[lines[index].id.v] = lines[index].id;
                previous.editable_order[index] = lines[index].id;
            }
        }
        previous.revision = editor.revision();
        previous.outline = Outline::empty(editor.revision());
        previous.rebuilt_block_count = rebuild_end - rebuild_start;
        previous.reused_block_count = lines.size() - previous.rebuilt_block_count;
        previous.incremental_update = true;
        previous.changed_block_indices.clear();
        previous.changed_block_indices.reserve(previous.rebuilt_block_count);
        for (auto index = rebuild_start; index < rebuild_end; ++index)
            previous.changed_block_indices.push_back(index);
        return previous;
    }

    RenderModel model;
    model.revision = editor.revision();
    model.outline = Outline::empty(editor.revision());
    model.blocks.reserve(lines.size());
    model.editable_order.reserve(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index >= rebuild_start && index < rebuild_end) {
            model.blocks.push_back(rebuild(index));
        } else {
            auto old_index = index < change.new_start
                ? index
                : change.old_end + (index - change.new_end);
            if (old_index >= previous.blocks.size()) return build_source_render_model(editor);
            model.blocks.push_back(std::move(previous.blocks[old_index]));
        }
        source_render_detail::append_editable_line(model, lines[index]);
    }
    model.rebuilt_block_count = rebuild_end - rebuild_start;
    model.reused_block_count = lines.size() - model.rebuilt_block_count;
    return model;
}

} // namespace elmd

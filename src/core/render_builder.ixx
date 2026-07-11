// elmd.core.render_builder — build_render_model(doc, source_text, outline).
// Walks the AST and produces a RenderModel, extracting markers (`**`/`*`/`~~`
// etc.) with the source-range accumulation method. Faithful port of the Rust
// builder — the layout later re-derives visibility from the live caret.
export module elmd.core.render_builder;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.ast;
import elmd.core.source_map;
import elmd.core.source_structure;
import elmd.core.document;
import elmd.core.outline;
import elmd.core.diagnostics;
import elmd.core.utf;
import elmd.core.render_model;

export namespace elmd {

inline RenderDiagnostic convert_diagnostic(const Diagnostic& d) {
    RenderDiagnostic r;
    r.severity = (d.severity == DiagnosticSeverity::Hint)   ? RenderDiagnostic::Sev::Info
               : (d.severity == DiagnosticSeverity::Error)   ? RenderDiagnostic::Sev::Error
               : RenderDiagnostic::Sev::Warning;
    r.message = d.message; r.source_range = d.source_range;
    return r;
}

inline RenderBlock render_block_base(BlockKind k, NodeId id, TextRange<CharOffset> sr, BlockStyle bs) {
    RenderBlock b; b.kind = [&](BlockKind)->RenderBlockKind {
        switch (k) {
            case BlockKind::Paragraph: case BlockKind::Heading:
            case BlockKind::List: case BlockKind::TaskList:
                return RenderBlockKind::Text;
            case BlockKind::BlockQuote: return RenderBlockKind::Quote;
            case BlockKind::CodeBlock: return RenderBlockKind::Code;
            case BlockKind::MathBlock: return RenderBlockKind::Math;
            case BlockKind::Table:     return RenderBlockKind::Table;
            case BlockKind::ImageBlock:return RenderBlockKind::Image;
            case BlockKind::Toc:       return RenderBlockKind::Toc;
            case BlockKind::Callout:   return RenderBlockKind::Callout;
            case BlockKind::FootnoteDefinition: return RenderBlockKind::Footnote;
            case BlockKind::Frontmatter:return RenderBlockKind::Frontmatter;
            case BlockKind::ThematicBreak: return RenderBlockKind::ThematicBreak;
            case BlockKind::LinkDefinition: return RenderBlockKind::Blank;
            case BlockKind::UnsupportedMarkup: return RenderBlockKind::Unsupported;
            case BlockKind::Extension: return RenderBlockKind::Extension;
        }
        return RenderBlockKind::Text;
    }(k);
    b.id = id; b.source_range = sr; b.block_style = bs;
    b.content_range = sr;
    return b;
}

// Build a Marker inline item at the running cursor; advances `cursor`.
inline void push_marker(std::vector<InlineRenderItem>& out, std::size_t& cursor, std::u32string text,
                        MarkerRole role = MarkerRole::Syntax, std::u32string display_text = {}) {
    InlineRenderItem m; m.kind = InlineRenderItem::Kind::Marker;
    m.source_range = CharRange(CharOffset(cursor), CharOffset(cursor + text.size()));
    m.text = std::move(text); m.display_text = std::move(display_text); m.marker_role = role;
    m.marker_style = MarkerStyle{true, {}}; m.visibility = MarkerVisibility::Always;
    out.push_back(std::move(m));
    cursor += out.back().text.size();
}

struct Builder {
    const SourceMap* sm = nullptr;
    std::u32string_view source;
    TextRange<CharOffset> node_source_range(NodeId id) const {
        if (const auto* range = sm ? sm->find_node_by_id(id) : nullptr) return range->source_range;
        return {};
    }
    TextRange<CharOffset> node_content_range(NodeId id) const {
        if (const auto* range = sm ? sm->find_node_by_id(id) : nullptr) return range->content_range;
        return {};
    }
    void append_block_break(std::vector<InlineRenderItem>& out, std::size_t source_offset) {
        std::size_t cursor = source_offset;
        push_marker(out, cursor, U"\n", MarkerRole::Structural);
    }
    void append_generated_indent(std::vector<InlineRenderItem>& out, std::size_t source_offset, std::size_t columns) {
        if (columns == 0) return;
        InlineRenderItem marker;
        marker.kind = InlineRenderItem::Kind::Marker;
        marker.source_range = CharRange(CharOffset(source_offset), CharOffset(source_offset));
        marker.display_text = std::u32string(columns, U' ');
        marker.marker_role = MarkerRole::Structural;
        marker.visibility = MarkerVisibility::Always;
        out.push_back(std::move(marker));
    }
    void append_list_contents(std::vector<InlineRenderItem>& out, const BlockNode& list, std::size_t depth, std::size_t indent_columns = 0) {
        auto append_items = [&](auto const& items, bool tasks) {
            for (std::size_t index = 0; index < items.size(); ++index) {
                auto const& item = items[index];
                auto item_source = node_source_range(item.id);
                auto item_content = node_content_range(item.id);
                std::size_t cursor = item_source.start.v;
                auto marker = item.marker;
                if (marker.empty()) {
                    if constexpr (requires { item.checked; }) marker = item.checked ? U"- [x] " : U"- [ ] ";
                    else if (list.list_ordered) marker = utf8_to_cps(std::to_string(list.list_start + index)) + std::u32string(1, list.list_delimiter) + U" ";
                    else marker = U"- ";
                }
                if (tasks) {
                    auto display = std::u32string(indent_columns, U' ') + (marker.empty() ? U"- [ ] " : marker);
                    push_marker(out, cursor, marker, MarkerRole::TaskCheckbox, std::move(display));
                } else if (list.list_ordered) {
                    auto display = std::u32string(indent_columns, U' ') + utf8_to_cps(std::to_string(list.list_start + index)) + std::u32string(1, list.list_delimiter) + U" ";
                    push_marker(out, cursor, marker, MarkerRole::ListNumber, std::move(display));
                } else {
                    push_marker(out, cursor, marker, MarkerRole::ListBullet, std::u32string(indent_columns, U' ') + U"\u2022 ");
                }
                bool first_child = true;
                for (auto const& child : item.children) {
                    auto child_source = node_source_range(child.id);
                    if (!first_child) append_block_break(out, child_source.start.v > 0 ? child_source.start.v - 1 : child_source.start.v);
                    auto marker_columns = tasks ? std::size_t{6}
                        : list.list_ordered ? std::to_string(list.list_start + index).size() + 2
                        : std::size_t{2};
                    append_nested_block(out, child, depth + 1, indent_columns + marker_columns);
                    first_child = false;
                }
                if (index + 1 < items.size()) {
                    auto newline = item_source.end.v > item_content.end.v && item_source.end.v > 0 ? item_source.end.v - 1 : item_source.end.v;
                    append_block_break(out, newline);
                }
            }
        };
        if (list.kind == BlockKind::TaskList) append_items(list.task_items, true);
        else append_items(list.list_items, false);
    }
    void append_nested_block(std::vector<InlineRenderItem>& out, const BlockNode& block, std::size_t depth, std::size_t indent_columns) {
        auto content = node_content_range(block.id);
        switch (block.kind) {
            case BlockKind::Paragraph:
            case BlockKind::Heading: {
                InlineStyle style = InlineStyle::plain();
                if (block.kind == BlockKind::Heading) style.heading_level = block.level;
                auto items = build_inlines(block.children, content.start.v, style);
                out.insert(out.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
                return;
            }
            case BlockKind::List:
            case BlockKind::TaskList:
                append_list_contents(out, block, depth, indent_columns);
                return;
            case BlockKind::BlockQuote:
            case BlockKind::Callout:
            case BlockKind::FootnoteDefinition: {
                if (block.kind == BlockKind::BlockQuote) {
                    if (const auto* range = sm ? sm->find_node_by_id(block.id) : nullptr; range && !range->marker_ranges.empty()) {
                        auto marker_range = range->marker_ranges.front();
                        auto start = (std::min)(marker_range.start.v, source.size());
                        auto end = (std::min)((std::max)(marker_range.end.v, start), source.size());
                        InlineRenderItem marker;
                        marker.kind = InlineRenderItem::Kind::Marker;
                        marker.source_range = marker_range;
                        marker.text = std::u32string(source.substr(start, end - start));
                        marker.display_text = std::u32string(indent_columns, U' ');
                        marker.marker_role = MarkerRole::Structural;
                        marker.visibility = MarkerVisibility::Always;
                        out.push_back(std::move(marker));
                    }
                }
                bool first = true;
                for (auto const& child : block.quote_children) {
                    auto child_source = node_source_range(child.id);
                    if (!first) append_block_break(out, child_source.start.v > 0 ? child_source.start.v - 1 : child_source.start.v);
                    append_nested_block(out, child, depth + 1, indent_columns);
                    first = false;
                }
                return;
            }
            case BlockKind::CodeBlock: {
                append_generated_indent(out, content.start.v, indent_columns);
                auto item = InlineRenderItem::plain_text(block.code_text, content);
                item.style.code = true;
                out.push_back(std::move(item));
                return;
            }
            case BlockKind::MathBlock: {
                InlineRenderItem item;
                item.kind = InlineRenderItem::Kind::Math;
                item.id = block.id;
                item.source_range = node_source_range(block.id);
                item.text = block.tex;
                item.display = MathDisplayMode::Block;
                item.math_delim = block.math_delim;
                out.push_back(std::move(item));
                return;
            }
            case BlockKind::ImageBlock: {
                InlineRenderItem item;
                item.kind = InlineRenderItem::Kind::Image;
                item.id = block.id;
                item.source_range = node_source_range(block.id);
                item.src = block.src;
                item.alt = block.image_alt;
                item.title = block.image_title;
                item.image_width = block.image_width;
                item.image_height = block.image_height;
                out.push_back(std::move(item));
                return;
            }
            case BlockKind::Table: {
                auto append_cell = [&](TableCell const& cell) {
                    auto cell_content = node_content_range(cell.id);
                    auto items = build_inlines(cell.children, cell_content.start.v, InlineStyle::plain());
                    out.insert(out.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
                };
                for (std::size_t index = 0; index < block.table_header.size(); ++index) {
                    if (index) append_block_break(out, node_source_range(block.table_header[index].id).start.v);
                    append_cell(block.table_header[index]);
                }
                for (auto const& row : block.table_rows) for (auto const& cell : row.cells) {
                    append_block_break(out, node_source_range(cell.id).start.v);
                    append_cell(cell);
                }
                return;
            }
            case BlockKind::UnsupportedMarkup: {
                auto item = InlineRenderItem::plain_text(utf8_to_cps(block.raw), node_source_range(block.id));
                out.push_back(std::move(item));
                return;
            }
            case BlockKind::ThematicBreak: {
                auto item = InlineRenderItem::plain_text(U"\u2014", node_source_range(block.id));
                out.push_back(std::move(item));
                return;
            }
            default:
                return;
        }
    }
    std::vector<InlineRenderItem> build_inlines(const InlineVec& nodes, std::size_t content_start, const InlineStyle& base) {
        std::vector<InlineRenderItem> out;
        std::size_t cursor = content_start;
        for (const auto& node : nodes) build_inline(node, out, cursor, base);
        return out;
    }
    void build_inline(const InlineNode& node, std::vector<InlineRenderItem>& out, std::size_t& cursor, const InlineStyle& style) {
        using K = InlineKind;
        auto range_for = [&](NodeId id, std::size_t fallback_end) -> TextRange<CharOffset> {
            if (const auto* r = sm ? sm->find_node_by_id(id) : nullptr) return r->source_range;
            return CharRange(CharOffset(cursor), CharOffset(fallback_end));
        };
        auto content_start_for = [&](NodeId id) -> std::size_t {
            if (const auto* r = sm ? sm->find_node_by_id(id) : nullptr) return r->content_range.start.v;
            return cursor;
        };
        auto source_text = [&](TextRange<CharOffset> range) {
            auto start = (std::min)(range.start.v, source.size());
            auto end = (std::min)((std::max)(range.end.v, start), source.size());
            return std::u32string(source.substr(start, end - start));
        };
        auto append_exact_marker = [&](TextRange<CharOffset> marker_range) {
            InlineRenderItem marker;
            marker.kind = InlineRenderItem::Kind::Marker;
            marker.marker_owner = node.id;
            marker.source_range = marker_range;
            marker.text = source_text(marker_range);
            marker.marker_style = MarkerStyle{true, {}};
            marker.visibility = MarkerVisibility::Always;
            out.push_back(std::move(marker));
        };
        auto append_delimited = [&](const InlineStyle& child_style, std::u32string fallback_opening,
                                    std::u32string fallback_closing) {
            if (const auto* range = sm ? sm->find_node_by_id(node.id) : nullptr;
                range && range->marker_ranges.size() >= 2) {
                append_exact_marker(range->marker_ranges.front());
                auto child_items = build_inlines(node.children, range->content_range.start.v, child_style);
                out.insert(out.end(), std::make_move_iterator(child_items.begin()), std::make_move_iterator(child_items.end()));
                append_exact_marker(range->marker_ranges.back());
                cursor = range->source_range.end.v;
                return;
            }
            if (!fallback_opening.empty()) {
                push_marker(out, cursor, std::move(fallback_opening));
                out.back().marker_owner = node.id;
            }
            auto child_items = build_inlines(node.children, cursor, child_style);
            if (!child_items.empty()) cursor = child_items.back().source_range.end.v;
            out.insert(out.end(), std::make_move_iterator(child_items.begin()), std::make_move_iterator(child_items.end()));
            if (!fallback_closing.empty()) {
                push_marker(out, cursor, std::move(fallback_closing));
                out.back().marker_owner = node.id;
            }
        };
        switch (node.kind) {
            case K::Text: {
                std::size_t len = node.text.size();
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it = InlineRenderItem::plain_text(node.text, sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor = sr.end.v;
                return;
            }
            case K::Strong: {
                InlineStyle cs = style; cs.bold = true;
                append_delimited(cs, node.opening_marker.empty() ? U"**" : node.opening_marker,
                                 node.closing_marker.empty() ? U"**" : node.closing_marker);
                return;
            }
            case K::Emphasis: {
                InlineStyle cs = style; cs.italic = true;
                append_delimited(cs, node.opening_marker.empty() ? U"*" : node.opening_marker,
                                 node.closing_marker.empty() ? U"*" : node.closing_marker);
                return;
            }
            case K::Strike: {
                InlineStyle cs = style; cs.strikethrough = true;
                append_delimited(cs, node.opening_marker.empty() ? U"~~" : node.opening_marker,
                                 node.closing_marker.empty() ? U"~~" : node.closing_marker);
                return;
            }
            case K::Span: {
                append_delimited(style, node.opening_marker, node.closing_marker);
                return;
            }
            case K::InlineCode: {
                const auto* range = sm ? sm->find_node_by_id(node.id) : nullptr;
                if (range && range->marker_ranges.size() == 2) {
                    InlineRenderItem opening;
                    opening.kind = InlineRenderItem::Kind::Marker;
                    opening.marker_owner = node.id;
                    opening.source_range = range->marker_ranges[0];
                    opening.text = source_text(opening.source_range);
                    opening.marker_style = MarkerStyle{true, {}};
                    opening.visibility = MarkerVisibility::Always;
                    out.push_back(std::move(opening));
                    InlineStyle cs = style; cs.code = true;
                    InlineRenderItem content = InlineRenderItem::plain_text(node.text, range->content_range);
                    content.style = cs;
                    out.push_back(std::move(content));
                    InlineRenderItem closing;
                    closing.kind = InlineRenderItem::Kind::Marker;
                    closing.marker_owner = node.id;
                    closing.source_range = range->marker_ranges[1];
                    closing.text = source_text(closing.source_range);
                    closing.marker_style = MarkerStyle{true, {}};
                    closing.visibility = MarkerVisibility::Always;
                    out.push_back(std::move(closing));
                    cursor = range->source_range.end.v;
                    return;
                }
                push_marker(out, cursor, U"`");
                out.back().marker_owner = node.id;
                InlineStyle cs = style; cs.code = true;
                InlineRenderItem it = InlineRenderItem::plain_text(node.text, CharRange(CharOffset(cursor), CharOffset(cursor + node.text.size())));
                it.style = cs;
                out.push_back(std::move(it));
                cursor += node.text.size();
                push_marker(out, cursor, U"`");
                out.back().marker_owner = node.id;
                return;
            }
            case K::InlineMath: {
                std::size_t len = node.text.size() + (node.math_delim == MathDelimiter::InlineParen ? 4 : 2);
                auto sr = range_for(node.id, cursor + len);
                MathDisplayMode disp = (node.math_delim == MathDelimiter::BlockDollar
                                        || node.math_delim == MathDelimiter::BlockBracket
                                        || node.math_delim == MathDelimiter::FencedMath)
                                       ? MathDisplayMode::Block : MathDisplayMode::Inline;
                InlineRenderItem it; it.kind = InlineRenderItem::Kind::Math;
                it.id = node.id; it.source_range = sr; it.text = node.text; it.display = disp; it.math_delim = node.math_delim;
                it.style.strikethrough = style.strikethrough;
                out.push_back(std::move(it));
                cursor = sr.end.v;
                return;
            }
            case K::Link: {
                std::size_t inner = content_start_for(node.id);
                InlineStyle ls = style; ls.link = true;
                auto child_items = build_inlines(node.children, inner, ls);
                std::size_t consumed = 0;
                if (const auto* r = sm ? sm->find_node_by_id(node.id) : nullptr) consumed = r->source_range.len();
                else {
                    // fallback: sum grandchildren text + 4
                    std::size_t sum = 0;
                    for (const auto& c : node.children) sum += inline_text_content(c).size();
                    consumed = sum + 4;
                }
                InlineRenderItem it; it.kind = InlineRenderItem::Kind::Link;
                it.id = node.id; it.source_range = range_for(node.id, cursor + consumed);
                it.href = node.href; it.title = node.title; it.children = std::move(child_items);
                out.push_back(std::move(it));
                cursor += consumed;
                return;
            }
            case K::Image: {
                std::size_t len = node.alt.size() + 5;
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it; it.kind = InlineRenderItem::Kind::Image;
                it.id = node.id; it.source_range = sr; it.src = node.href; it.alt = node.alt;
                it.title = node.title;
                it.image_width = node.image_width; it.image_height = node.image_height;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::FootnoteRef: {
                std::u32string raw = U"[^" + (node.label.empty() ? U"" : utf8_to_cps(node.label));
                raw.push_back(']');
                std::size_t len = raw.size();
                auto sr = range_for(node.id, cursor + len);
                InlineStyle ls = style; ls.link = true;
                InlineRenderItem it = InlineRenderItem::plain_text(raw, sr);
                it.style = ls;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::WikiLink: {
                std::u32string raw;
                raw = U"[[" + utf8_to_cps(node.target);
                if (node.alias) { raw.push_back('|'); raw += utf8_to_cps(*node.alias); }
                raw += U"]]";
                std::size_t len = raw.size();
                auto sr = range_for(node.id, cursor + len);
                InlineStyle ls = style; ls.link = true;
                InlineRenderItem it = InlineRenderItem::plain_text(raw, sr);
                it.style = ls;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::SoftBreak: {
                auto sr = range_for(node.id, cursor + 1);
                InlineRenderItem it = InlineRenderItem::plain_text(U" ", sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor = sr.end.v;
                return;
            }
            case K::HardBreak: {
                const auto* range = sm ? sm->find_node_by_id(node.id) : nullptr;
                auto sr = range ? range->content_range : range_for(node.id, cursor + 1);
                InlineRenderItem it = InlineRenderItem::plain_text(U"\n", sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor = sr.end.v;
                return;
            }
            case K::UnsupportedMarkup: {
                std::size_t len = node.text.size();
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it = InlineRenderItem::plain_text(node.text, sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::Extension: {
                std::u32string raw = node.ext_text.empty()
                    ? U"[ext:" + utf8_to_cps(node.ext_name) + U"]"
                    : U"[ext:" + utf8_to_cps(node.ext_name) + U":" + node.ext_text + U"]";
                std::size_t len = raw.size();
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it = InlineRenderItem::plain_text(raw, sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
        }
    }

    std::size_t list_marker_columns(const BlockNode& list, std::size_t index) const {
        if (list.kind == BlockKind::TaskList) return 6;
        if (list.list_ordered) return std::to_string(list.list_start + index).size() + 2;
        return 2;
    }

    void collect_nested_blocks(RenderBlock& parent, const BlockNode& child, std::size_t depth, std::size_t indent_columns) {
        if (child.kind == BlockKind::CodeBlock || child.kind == BlockKind::BlockQuote) {
            auto rendered = build_block(child);
            rendered.container_depth = depth;
            rendered.container_indent_columns = indent_columns;
            parent.child_blocks.push_back(std::move(rendered));
            return;
        }
        if (child.kind == BlockKind::List) {
            for (std::size_t index = 0; index < child.list_items.size(); ++index) {
                auto const& item = child.list_items[index];
                auto next_indent = indent_columns + list_marker_columns(child, index);
                for (auto const& nested : item.children) collect_nested_blocks(parent, nested, depth + 1, next_indent);
            }
            return;
        }
        if (child.kind == BlockKind::TaskList) {
            for (std::size_t index = 0; index < child.task_items.size(); ++index) {
                auto const& item = child.task_items[index];
                auto next_indent = indent_columns + list_marker_columns(child, index);
                for (auto const& nested : item.children) collect_nested_blocks(parent, nested, depth + 1, next_indent);
            }
            return;
        }
        if (child.kind == BlockKind::Callout || child.kind == BlockKind::FootnoteDefinition) {
            for (auto const& nested : child.quote_children) collect_nested_blocks(parent, nested, depth, indent_columns);
        }
    }

    RenderBlock build_block(const BlockNode& b) {
        using BK = BlockKind;
        auto base_range = [&]() -> TextRange<CharOffset> {
            if (const auto* r = sm ? sm->find_node_by_id(b.id) : nullptr) return r->source_range;
            return CharRange(CharOffset(0), CharOffset(0));
        };
        auto content_range = [&]() -> TextRange<CharOffset> {
            if (const auto* r = sm ? sm->find_node_by_id(b.id) : nullptr) return r->content_range;
            return base_range();
        };
        auto with_content_range = [&](RenderBlock rb) {
            rb.content_range = content_range();
            return rb;
        };
        switch (b.kind) {
            case BK::Paragraph: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
                InlineStyle s = InlineStyle::plain();
                auto cursor = base_range().start.v;
                if (!b.opening_marker.empty()) { push_marker(rb.inline_items, cursor, b.opening_marker); rb.inline_items.back().marker_owner = b.id; }
                auto items = build_inlines(b.children, content_range().start.v, s);
                for (auto& item : items) rb.inline_items.push_back(std::move(item));
                if (!b.closing_marker.empty()) {
                    cursor = content_range().end.v;
                    push_marker(rb.inline_items, cursor, b.closing_marker);
                    rb.inline_items.back().marker_owner = b.id;
                }
                return with_content_range(std::move(rb));
            }
            case BK::Heading: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::heading(b.level));
                InlineStyle s = InlineStyle::plain(); s.heading_level = b.level;
                std::size_t cs = base_range().start.v;
                auto range = sm ? sm->find_node_by_id(b.id) : nullptr;
                if (range) cs = range->content_range.start.v;
                if (range) {
                    for (auto const& markerRange : range->marker_ranges) {
                        auto start = (std::min)(markerRange.start.v, source.size());
                        auto end = (std::min)((std::max)(markerRange.end.v, start), source.size());
                        InlineRenderItem marker;
                        marker.kind = InlineRenderItem::Kind::Marker;
                        marker.source_range = markerRange;
                        marker.text = std::u32string(source.substr(start, end - start));
                        marker.style = s;
                        marker.marker_role = MarkerRole::Heading;
                        marker.marker_owner = b.id;
                        marker.marker_style = MarkerStyle{true, {}};
                        marker.visibility = MarkerVisibility::WhenBlockFocused;
                        rb.inline_items.push_back(std::move(marker));
                    }
                }
                auto items = build_inlines(b.children, cs, s);
                for (auto& it : items) rb.inline_items.push_back(std::move(it));
                std::stable_sort(rb.inline_items.begin(), rb.inline_items.end(), [](auto const& left, auto const& right) {
                    return left.source_range.start.v < right.source_range.start.v;
                });
                return with_content_range(std::move(rb));
            }
            case BK::BlockQuote: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::blockquote());
                std::function<void(const BlockNode&, std::size_t)> append_quote;
                append_quote = [&](const BlockNode& quote, std::size_t depth) {
                    for (const auto& child : quote.quote_children) {
                        if (child.kind == BlockKind::BlockQuote) {
                            append_quote(child, depth + 1);
                            continue;
                        }
                        auto rendered = build_block(child);
                        rendered.quote_depth = depth;
                        rb.child_blocks.push_back(std::move(rendered));
                    }
                    const auto* range = sm ? sm->find_node_by_id(quote.id) : nullptr;
                    if (!range || range->marker_ranges.empty() || range->marker_ranges.back().end != range->content_range.end) return;
                    bool covered = false;
                    if (!rb.child_blocks.empty()) {
                        auto const& last = rb.child_blocks.back();
                        covered = last.quote_depth == depth && !last.inline_items.empty() && last.inline_items.back().kind == InlineRenderItem::Kind::Text && last.inline_items.back().text == U"\n";
                    }
                    if (covered) return;
                    RenderBlock blank;
                    blank.kind = RenderBlockKind::Blank;
                    blank.id = NodeId(0x9000000000000000ull | ((range->content_range.end.v + depth) & 0x0fffffffffffffffull));
                    blank.source_range = CharRange(range->content_range.end, range->content_range.end);
                    blank.content_range = blank.source_range;
                    blank.block_style = BlockStyle::paragraph();
                    blank.quote_depth = depth;
                    rb.child_blocks.push_back(std::move(blank));
                };
                append_quote(b, 0);
                return with_content_range(std::move(rb));
            }
            case BK::List: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::list());
                append_list_contents(rb.inline_items, b, 0);
                for (std::size_t index = 0; index < b.list_items.size(); ++index) {
                    auto const& item = b.list_items[index];
                    auto indent = list_marker_columns(b, index);
                    for (auto const& child : item.children) collect_nested_blocks(rb, child, 1, indent);
                }
                return with_content_range(std::move(rb));
            }
            case BK::TaskList: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::list());
                append_list_contents(rb.inline_items, b, 0);
                for (std::size_t index = 0; index < b.task_items.size(); ++index) {
                    auto const& item = b.task_items[index];
                    auto indent = list_marker_columns(b, index);
                    for (auto const& child : item.children) collect_nested_blocks(rb, child, 1, indent);
                }
                return with_content_range(std::move(rb));
            }
            case BK::CodeBlock: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::code());
                rb.language = b.language;
                rb.code_text = b.code_text;
                rb.code_indented = b.code_indented;
                if (b.code_indented) {
                    if (const auto* range = sm ? sm->find_node_by_id(b.id) : nullptr) rb.code_marker_ranges = range->marker_ranges;
                }
                std::size_t n = 1; for (char32_t c : b.code_text) if (c == '\n') ++n;
                rb.line_count = n;
                return with_content_range(std::move(rb));
            }
            case BK::MathBlock: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::math());
                rb.tex = b.tex; rb.math_delim = b.math_delim;
                return with_content_range(std::move(rb));
            }
            case BK::Table: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::table());
                rb.table_aligns = b.table_aligns;
                rb.table_header_row = b.table_header_row;
                rb.column_count = std::max(b.table_header.size(), b.table_rows.empty() ? 0 : b.table_rows[0].cells.size());
                rb.row_count = 1 + b.table_rows.size();
                auto build_cell = [&](const TableCell& cell) {
                    std::size_t start = base_range().start.v;
                    auto cell_range = base_range();
                    if (const auto* range = sm ? sm->find_node_by_id(cell.id) : nullptr) { start = range->content_range.start.v; cell_range = range->content_range; }
                    rb.table_cell_ranges.push_back(cell_range);
                    return build_inlines(cell.children, start, InlineStyle::plain());
                };
                for (const auto& c : b.table_header) {
                    rb.table_cells.push_back(build_cell(c));
                }
                for (const auto& row : b.table_rows) {
                    for (const auto& c : row.cells) {
                        rb.table_cells.push_back(build_cell(c));
                    }
                }
                return with_content_range(std::move(rb));
            }
            case BK::ImageBlock: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::image());
                rb.alt = b.image_alt; rb.src = b.src; rb.title = b.image_title; rb.link = b.image_link;
                rb.image_width = b.image_width; rb.image_height = b.image_height;
                return with_content_range(std::move(rb));
            }
            case BK::Callout: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::callout(b.callout_kind));
                rb.callout_kind = b.callout_kind;
                if (b.callout_title) {
                    std::vector<InlineRenderItem> items;
                    items = build_inlines(*b.callout_title, base_range().start.v, InlineStyle::plain());
                    rb.callout_title = std::move(items);
                }
                for (const auto& ch : b.quote_children) rb.child_blocks.push_back(build_block(ch));
                return with_content_range(std::move(rb));
            }
            case BK::FootnoteDefinition: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::footnote());
                rb.footnote_label = b.footnote_label;
                for (const auto& ch : b.quote_children) rb.child_blocks.push_back(build_block(ch));
                return with_content_range(std::move(rb));
            }
            case BK::Toc: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::toc());
                return with_content_range(std::move(rb));
            }
            case BK::Frontmatter: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::frontmatter());
                rb.raw = b.raw;
                return with_content_range(std::move(rb));
            }
            case BK::ThematicBreak: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::thematic_break());
                return with_content_range(std::move(rb));
            }
            case BK::LinkDefinition: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
                return with_content_range(std::move(rb));
            }
            case BK::UnsupportedMarkup: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::unsupported());
                rb.raw = b.raw;
                rb.reason_text = unsupported_reason_message(b.unsup_reason);
                return with_content_range(std::move(rb));
            }
            case BK::Extension: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::extension());
                rb.extension_name = b.ext_name;
                return with_content_range(std::move(rb));
            }
        }
        return render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
    }
};

inline RenderModel build_render_model(const EditorDocument& doc,
                                      const std::string& source_text,
                                      const Outline& outline) {
    Builder bd; bd.sm = &doc.source_map;
    std::vector<RenderBlock> blocks;
    auto source = utf8_to_cps(source_text);
    bd.source = source;
    auto structure = build_source_structure(doc, source);
    for (const auto& span : structure.blocks) {
        if (span.kind == SourceBlockKind::Semantic && span.document_block_index && *span.document_block_index < doc.blocks.size()) {
            auto rendered = bd.build_block(doc.blocks[*span.document_block_index]);
            auto start = (std::min)(rendered.source_range.start.v, source.size());
            auto end = (std::min)((std::max)(rendered.source_range.end.v, start), source.size());
            rendered.source_fingerprint = static_cast<std::uint64_t>(std::hash<std::u32string_view>{}(std::u32string_view(source).substr(start, end - start)));
            blocks.push_back(std::move(rendered));
            continue;
        }
        RenderBlock blank;
        blank.kind = RenderBlockKind::Blank;
        blank.id = NodeId(0x8000000000000000ull | (span.content_range.start.v & 0x7fffffffffffffffull));
        blank.source_range = span.source_range;
        blank.content_range = span.content_range;
        auto start = (std::min)(span.source_range.start.v, source.size());
        auto end = (std::min)((std::max)(span.source_range.end.v, start), source.size());
        blank.source_fingerprint = static_cast<std::uint64_t>(std::hash<std::u32string_view>{}(std::u32string_view(source).substr(start, end - start)));
        blank.block_style = BlockStyle::paragraph();
        blocks.push_back(std::move(blank));
    }
    std::vector<RenderDiagnostic> diags;
    for (const auto& d : doc.diagnostics) diags.push_back(convert_diagnostic(d));
    RenderModel m; m.revision = doc.revision; m.blocks = std::move(blocks);
    m.outline = outline; m.diagnostics = std::move(diags);
    return m;
}

} // namespace elmd

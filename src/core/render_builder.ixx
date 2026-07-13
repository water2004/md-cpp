// elmd.core.render_builder — derive a local-coordinate RenderModel directly
// from the authoritative block tree and each node's lossless inline source.
export module elmd.core.render_builder;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.ast;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.text_edit;
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
    r.message = d.message; r.source_span = d.source_span;
    return r;
}

inline std::size_t block_local_length(const BlockNode& block) {
    switch (block.kind) {
        case BlockKind::Paragraph:
        case BlockKind::Heading:
        case BlockKind::TableCell:
            return block.inline_content.source.size();
        case BlockKind::CodeBlock:
            return block.code_text.size();
        case BlockKind::MathBlock:
            return block.tex.size();
        case BlockKind::Frontmatter:
        case BlockKind::UnsupportedMarkup:
        case BlockKind::LinkDefinition:
            return utf8_to_cps(block.raw).size();
        case BlockKind::ImageBlock:
        case BlockKind::Toc:
        case BlockKind::ThematicBreak:
        case BlockKind::Extension:
            return 1;
        default:
            return 0;
    }
}

inline RenderBlock render_block_base(BlockKind k, NodeId id, std::size_t length, BlockStyle bs) {
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
    b.id = id; b.source_span = {id, {0, length}}; b.block_style = bs;
    b.content_span = b.source_span;
    return b;
}

// Build a Marker inline item at the running cursor; advances `cursor`.
inline void push_marker(std::vector<InlineRenderItem>& out, NodeId owner, std::size_t& cursor, std::u32string text,
                        MarkerRole role = MarkerRole::Syntax, std::u32string display_text = {}) {
    InlineRenderItem m; m.kind = InlineRenderItem::Kind::Marker;
    m.source_span = {owner, {cursor, cursor}};
    m.text = std::move(text); m.display_text = std::move(display_text); m.marker_role = role;
    m.marker_style = MarkerStyle{true, {}}; m.visibility = MarkerVisibility::Always;
    out.push_back(std::move(m));
}

struct Builder {
    std::optional<NodeId> first_editable_owner(const BlockNode& block) const {
        if (block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading
            || block.kind == BlockKind::TableCell || block.kind == BlockKind::CodeBlock
            || block.kind == BlockKind::MathBlock) return block.id;
        for (auto const& child : block.children) {
            if (auto owner = first_editable_owner(child)) return owner;
        }
        return std::nullopt;
    }

    void append_block_break(std::vector<InlineRenderItem>& out, NodeId owner, std::size_t source_offset = 0) {
        std::size_t cursor = source_offset;
        push_marker(out, owner, cursor, U"\n", MarkerRole::Structural);
    }
    void append_generated_indent(std::vector<InlineRenderItem>& out, NodeId owner, std::size_t source_offset, std::size_t columns) {
        if (columns == 0) return;
        InlineRenderItem marker;
        marker.kind = InlineRenderItem::Kind::Marker;
        marker.source_span = {owner, {source_offset, source_offset}};
        marker.display_text = std::u32string(columns, U' ');
        marker.marker_role = MarkerRole::Structural;
        marker.visibility = MarkerVisibility::Always;
        out.push_back(std::move(marker));
    }
    void append_list_contents(std::vector<InlineRenderItem>& out, const BlockNode& list, std::size_t depth, std::size_t indent_columns = 0) {
        auto append_items = [&](const BlockVec& items, bool tasks) {
            for (std::size_t index = 0; index < items.size(); ++index) {
                auto const& item = items[index];
                const auto owner = item.children.empty() ? item.id : item.children.front().id;
                std::size_t cursor = 0;
                auto marker = item.marker;
                if (marker.empty()) {
                    if (tasks) marker = item.checked ? U"- [x] " : U"- [ ] ";
                    else if (list.list_ordered) marker = utf8_to_cps(std::to_string(list.list_start + index)) + std::u32string(1, list.list_delimiter) + U" ";
                    else marker = U"- ";
                }
                if (tasks) {
                    auto display = std::u32string(indent_columns, U' ') + (marker.empty() ? U"- [ ] " : marker);
                    push_marker(out, owner, cursor, marker, MarkerRole::TaskCheckbox, std::move(display));
                } else if (list.list_ordered) {
                    auto display = std::u32string(indent_columns, U' ') + utf8_to_cps(std::to_string(list.list_start + index)) + std::u32string(1, list.list_delimiter) + U" ";
                    push_marker(out, owner, cursor, marker, MarkerRole::ListNumber, std::move(display));
                } else {
                    push_marker(out, owner, cursor, marker, MarkerRole::ListBullet, std::u32string(indent_columns, U' ') + U"\u2022 ");
                }
                bool first_child = true;
                for (auto const& child : item.children) {
                    if (!first_child) append_block_break(out, child.id, block_local_length(child));
                    auto marker_columns = tasks ? std::size_t{6}
                        : list.list_ordered ? std::to_string(list.list_start + index).size() + 2
                        : std::size_t{2};
                    append_nested_block(out, child, depth + 1, indent_columns + marker_columns);
                    first_child = false;
                }
                if (index + 1 < items.size()) {
                    const auto offset = item.children.empty() ? std::size_t{0} : block_local_length(item.children.back());
                    append_block_break(out, owner, offset);
                }
            }
        };
        append_items(list.children, list.kind == BlockKind::TaskList);
    }
    void append_nested_block(std::vector<InlineRenderItem>& out, const BlockNode& block, std::size_t depth, std::size_t indent_columns) {
        switch (block.kind) {
            case BlockKind::Paragraph:
            case BlockKind::Heading: {
                InlineStyle style = InlineStyle::plain();
                if (block.kind == BlockKind::Heading) style.heading_level = block.level;
                auto items = build_inline_document(block.inline_content, block.id, style);
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
                bool first = true;
                for (auto const& child : block.children) {
                    if (!first) append_block_break(out, child.id, block_local_length(child));
                    if (block.kind == BlockKind::BlockQuote) {
                        auto owner = first_editable_owner(child).value_or(child.id);
                        append_generated_indent(out, owner, 0, indent_columns + 2);
                    }
                    append_nested_block(out, child, depth + 1, indent_columns);
                    first = false;
                }
                return;
            }
            case BlockKind::CodeBlock: {
                append_generated_indent(out, block.id, 0, indent_columns);
                auto item = InlineRenderItem::plain_text(block.code_text, {block.id, {0, block.code_text.size()}});
                item.style.code = true;
                out.push_back(std::move(item));
                return;
            }
            case BlockKind::MathBlock: {
                InlineRenderItem item;
                item.kind = InlineRenderItem::Kind::Math;
                item.id = block.id;
                item.source_span = {block.id, {0, block.tex.size()}};
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
                item.source_span = {block.id, {0, 0}};
                item.src = block.src;
                item.alt = block.image_alt;
                item.title = block.image_title;
                item.image_width = block.image_width;
                item.image_height = block.image_height;
                out.push_back(std::move(item));
                return;
            }
            case BlockKind::Table: {
                auto append_cell = [&](BlockNode const& cell) {
                    auto items = build_inline_document(cell.inline_content, cell.id, InlineStyle::plain());
                    out.insert(out.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
                };
                bool first_cell = true;
                for (const auto& row : block.children) for (const auto& cell : row.children) {
                    if (!first_cell) append_block_break(out, cell.id, 0);
                    append_cell(cell);
                    first_cell = false;
                }
                return;
            }
            case BlockKind::UnsupportedMarkup: {
                auto raw = utf8_to_cps(block.raw);
                auto item = InlineRenderItem::plain_text(raw, {block.id, {0, raw.size()}});
                out.push_back(std::move(item));
                return;
            }
            case BlockKind::ThematicBreak: {
                auto item = InlineRenderItem::plain_text(U"\u2014", {block.id, {0, 0}});
                out.push_back(std::move(item));
                return;
            }
            default:
                return;
        }
    }
    std::vector<InlineRenderItem> build_inline_document(
        const InlineDocument& document,
        NodeId owner_id,
        const InlineStyle& base) {
        std::vector<InlineRenderItem> output;
        auto source_span = [&](SourceRange range) {
            return TextSpan{owner_id, range};
        };
        auto append_marker = [&](std::vector<InlineRenderItem>& target, const InlineCstNode& owner, SourceRange range) {
            if (range.empty()) return;
            InlineRenderItem marker;
            marker.kind = InlineRenderItem::Kind::Marker;
            marker.id = owner.id;
            marker.marker_owner = owner.id;
            marker.source_span = source_span(range);
            marker.text = inline_source_slice(document, range);
            marker.marker_style = MarkerStyle{true, {}};
            marker.visibility = MarkerVisibility::Always;
            target.push_back(std::move(marker));
        };
        std::function<void(const InlineCstNodes&, const InlineStyle&, std::vector<InlineRenderItem>&)> append_nodes;
        append_nodes = [&](const InlineCstNodes& nodes, const InlineStyle& style, std::vector<InlineRenderItem>& target) {
            for (const auto& node : nodes) {
                using K = InlineCstKind;
                auto append_text = [&](std::u32string text, SourceRange range, InlineStyle text_style = InlineStyle::plain()) {
                    InlineRenderItem item = InlineRenderItem::plain_text(std::move(text), source_span(range));
                    item.id = node.id;
                    item.style = text_style;
                    target.push_back(std::move(item));
                };
                switch (node.kind) {
                    case K::Strong: {
                        auto child_style = style; child_style.bold = true;
                        append_marker(target, node, node.delim.opening);
                        append_nodes(node.children, child_style, target);
                        if (node.delim.closing) append_marker(target, node, *node.delim.closing);
                        break;
                    }
                    case K::Emphasis: {
                        auto child_style = style; child_style.italic = true;
                        append_marker(target, node, node.delim.opening);
                        append_nodes(node.children, child_style, target);
                        if (node.delim.closing) append_marker(target, node, *node.delim.closing);
                        break;
                    }
                    case K::Strikethrough: {
                        auto child_style = style; child_style.strikethrough = true;
                        append_marker(target, node, node.delim.opening);
                        append_nodes(node.children, child_style, target);
                        if (node.delim.closing) append_marker(target, node, *node.delim.closing);
                        break;
                    }
                    case K::CodeSpan: {
                        auto child_style = style; child_style.code = true;
                        append_marker(target, node, node.delim.opening);
                        append_text(inline_source_slice(document, node.delim.content), node.delim.content, child_style);
                        if (node.delim.closing) append_marker(target, node, *node.delim.closing);
                        break;
                    }
                    case K::InlineMath: {
                        InlineRenderItem item;
                        item.kind = InlineRenderItem::Kind::Math;
                        item.id = node.id;
                        item.source_span = source_span(node.range);
                        item.text = inline_source_slice(document, node.delim.content);
                        item.display = MathDisplayMode::Inline;
                        item.math_delim = node.math_delim;
                        item.style = style;
                        item.style.bold = false;
                        item.style.italic = false;
                        item.style.code = false;
                        item.style.link = false;
                        target.push_back(std::move(item));
                        break;
                    }
                    case K::Link:
                    case K::Autolink: {
                        InlineRenderItem item;
                        item.kind = InlineRenderItem::Kind::Link;
                        item.id = node.id;
                        item.source_span = source_span(node.range);
                        item.href = node.href;
                        item.title = node.title;
                        auto child_style = style; child_style.link = true;
                        if (node.kind == K::Link) {
                            append_marker(item.children, node, node.delim.opening);
                            append_nodes(node.children, child_style, item.children);
                            if (node.delim.closing) append_marker(item.children, node, *node.delim.closing);
                        } else {
                            append_marker(item.children, node, node.delim.opening);
                            InlineRenderItem text_item = InlineRenderItem::plain_text(
                                inline_source_slice(document, node.delim.content), source_span(node.delim.content));
                            text_item.style = child_style;
                            item.children.push_back(std::move(text_item));
                            if (node.delim.closing) append_marker(item.children, node, *node.delim.closing);
                        }
                        target.push_back(std::move(item));
                        break;
                    }
                    case K::Image: {
                        InlineRenderItem item;
                        item.kind = InlineRenderItem::Kind::Image;
                        item.id = node.id;
                        item.source_span = source_span(node.range);
                        item.src = node.href;
                        item.alt = node.alt;
                        item.title = node.title;
                        item.image_width = node.image_width;
                        item.image_height = node.image_height;
                        target.push_back(std::move(item));
                        break;
                    }
                    case K::HtmlElement:
                        append_marker(target, node, node.delim.opening);
                        append_nodes(node.children, style, target);
                        if (node.delim.closing) append_marker(target, node, *node.delim.closing);
                        break;
                    case K::Escape: {
                        const auto raw = inline_source_slice(document, node.range);
                        if (!raw.empty()) {
                            append_marker(target, node, {node.range.start, node.range.start + 1});
                            append_text(raw.substr(1), {node.range.start + 1, node.range.end}, style);
                        }
                        break;
                    }
                    case K::Entity:
                        append_text(decode_inline_entity(inline_source_slice(document, node.range)), node.range, style);
                        break;
                    case K::SoftBreak:
                        append_text(U" ", node.range, style);
                        break;
                    case K::HardBreak:
                        append_text(U"\n", node.range, style);
                        break;
                    case K::FootnoteRef:
                        append_text(U"[^" + utf8_to_cps(node.label) + U"]", node.range, InlineStyle{.link = true});
                        break;
                    case K::WikiLink:
                        append_text(utf8_to_cps(node.alias.value_or(node.target)), node.range, InlineStyle{.link = true});
                        break;
                    case K::Incomplete:
                        append_marker(target, node, node.delim.opening);
                        if (!node.delim.content.empty()) append_text(inline_source_slice(document, node.delim.content), node.delim.content, style);
                        break;
                    default:
                        append_text(inline_source_slice(document, node.range), node.range, style);
                        break;
                }
            }
        };
        append_nodes(document.tree.nodes, base, output);
        auto attach_source = [&](auto& self, std::vector<InlineRenderItem>& items) -> void {
            for (auto& item : items) {
                item.source_text = inline_source_slice(document, item.source_span.source_range);
                self(self, item.children);
            }
        };
        attach_source(attach_source, output);
        return output;
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
            for (std::size_t index = 0; index < child.children.size(); ++index) {
                auto const& item = child.children[index];
                auto next_indent = indent_columns + list_marker_columns(child, index);
                for (auto const& nested : item.children) collect_nested_blocks(parent, nested, depth + 1, next_indent);
            }
            return;
        }
        if (child.kind == BlockKind::TaskList) {
            for (std::size_t index = 0; index < child.children.size(); ++index) {
                auto const& item = child.children[index];
                auto next_indent = indent_columns + list_marker_columns(child, index);
                for (auto const& nested : item.children) collect_nested_blocks(parent, nested, depth + 1, next_indent);
            }
            return;
        }
        if (child.kind == BlockKind::Callout || child.kind == BlockKind::FootnoteDefinition) {
            for (auto const& nested : child.children) collect_nested_blocks(parent, nested, depth, indent_columns);
        }
    }

    RenderBlock build_block(const BlockNode& b) {
        using BK = BlockKind;
        auto base = [&](BlockStyle style) {
            return render_block_base(b.kind, b.id, block_local_length(b), std::move(style));
        };
        switch (b.kind) {
            case BK::Paragraph: {
                auto rb = base(BlockStyle::paragraph());
                if (b.inline_content.source.empty() && b.opening_marker.empty() && b.closing_marker.empty()) {
                    rb.kind = RenderBlockKind::Blank;
                    return rb;
                }
                InlineStyle s = InlineStyle::plain();
                std::size_t cursor = 0;
                if (!b.opening_marker.empty()) { cursor = 0; push_marker(rb.inline_items, b.id, cursor, b.opening_marker); rb.inline_items.back().marker_owner = b.id; }
                auto items = build_inline_document(b.inline_content, b.id, s);
                for (auto& item : items) rb.inline_items.push_back(std::move(item));
                if (!b.closing_marker.empty()) {
                    cursor = b.inline_content.source.size();
                    push_marker(rb.inline_items, b.id, cursor, b.closing_marker);
                    rb.inline_items.back().marker_owner = b.id;
                }
                return rb;
            }
            case BK::Heading: {
                auto rb = base(BlockStyle::heading(b.level));
                InlineStyle s = InlineStyle::plain(); s.heading_level = b.level;
                auto append_heading_marker = [&](std::u32string const& text, std::size_t offset) {
                    if (text.empty()) return;
                    InlineRenderItem marker;
                    marker.kind = InlineRenderItem::Kind::Marker;
                    marker.source_span = {b.id, {offset, offset}};
                    marker.text = text;
                    marker.style = s;
                    marker.marker_role = MarkerRole::Heading;
                    marker.marker_owner = b.id;
                    marker.marker_style = MarkerStyle{true, {}};
                    marker.visibility = MarkerVisibility::WhenBlockFocused;
                    rb.inline_items.push_back(std::move(marker));
                };
                append_heading_marker(b.opening_marker, 0);
                auto items = build_inline_document(b.inline_content, b.id, s);
                for (auto& it : items) rb.inline_items.push_back(std::move(it));
                append_heading_marker(b.closing_marker, b.inline_content.source.size());
                std::stable_sort(rb.inline_items.begin(), rb.inline_items.end(), [](auto const& left, auto const& right) {
                    return left.source_span.source_range.start < right.source_span.source_range.start;
                });
                return rb;
            }
            case BK::BlockQuote: {
                auto rb = base(BlockStyle::blockquote());
                std::function<void(const BlockNode&, std::size_t)> append_quote;
                append_quote = [&](const BlockNode& quote, std::size_t depth) {
                    for (const auto& child : quote.children) {
                        if (child.kind == BlockKind::BlockQuote) {
                            append_quote(child, depth + 1);
                            continue;
                        }
                        auto rendered = build_block(child);
                        rendered.quote_depth = depth;
                        rb.child_blocks.push_back(std::move(rendered));
                    }
                };
                append_quote(b, 0);
                return rb;
            }
            case BK::List: {
                auto rb = base(BlockStyle::list());
                append_list_contents(rb.inline_items, b, 0);
                for (std::size_t index = 0; index < b.children.size(); ++index) {
                    auto const& item = b.children[index];
                    auto indent = list_marker_columns(b, index);
                    for (auto const& child : item.children) collect_nested_blocks(rb, child, 1, indent);
                }
                return rb;
            }
            case BK::TaskList: {
                auto rb = base(BlockStyle::list());
                append_list_contents(rb.inline_items, b, 0);
                for (std::size_t index = 0; index < b.children.size(); ++index) {
                    auto const& item = b.children[index];
                    auto indent = list_marker_columns(b, index);
                    for (auto const& child : item.children) collect_nested_blocks(rb, child, 1, indent);
                }
                return rb;
            }
            case BK::CodeBlock: {
                auto rb = base(BlockStyle::code());
                rb.language = b.language;
                rb.code_text = b.code_text;
                rb.code_indented = b.code_indented;
                rb.opening_marker = b.opening_marker;
                rb.closing_marker = b.closing_marker;
                std::size_t n = 1; for (char32_t c : b.code_text) if (c == '\n') ++n;
                rb.line_count = n;
                return rb;
            }
            case BK::MathBlock: {
                auto rb = base(BlockStyle::math());
                rb.tex = b.tex; rb.math_delim = b.math_delim;
                return rb;
            }
            case BK::Table: {
                auto rb = base(BlockStyle::table());
                rb.table_aligns = b.table_aligns;
                rb.table_header_row = !b.children.empty() && b.children.front().table_header_row;
                rb.column_count = b.children.empty() ? 0 : b.children.front().children.size();
                rb.row_count = b.children.size();
                auto build_cell = [&](const BlockNode& cell) {
                    rb.table_cell_spans.push_back({cell.id, {0, cell.inline_content.source.size()}});
                    return build_inline_document(cell.inline_content, cell.id, InlineStyle::plain());
                };
                for (const auto& row : b.children) for (const auto& cell : row.children) rb.table_cells.push_back(build_cell(cell));
                return rb;
            }
            case BK::ImageBlock: {
                auto rb = base(BlockStyle::image());
                rb.alt = b.image_alt; rb.src = b.src; rb.title = b.image_title; rb.link = b.image_link;
                rb.image_width = b.image_width; rb.image_height = b.image_height;
                return rb;
            }
            case BK::Callout: {
                auto rb = base(BlockStyle::callout(b.callout_kind));
                rb.callout_kind = b.callout_kind;
                if (b.callout_title) {
                    std::vector<InlineRenderItem> items;
                    items = build_inline_document(*b.callout_title, b.id, InlineStyle::plain());
                    rb.callout_title = std::move(items);
                }
                for (const auto& ch : b.children) rb.child_blocks.push_back(build_block(ch));
                return rb;
            }
            case BK::FootnoteDefinition: {
                auto rb = base(BlockStyle::footnote());
                rb.footnote_label = b.footnote_label;
                for (const auto& ch : b.children) rb.child_blocks.push_back(build_block(ch));
                return rb;
            }
            case BK::Toc: {
                return base(BlockStyle::toc());
            }
            case BK::Frontmatter: {
                auto rb = base(BlockStyle::frontmatter());
                rb.raw = b.raw;
                return rb;
            }
            case BK::ThematicBreak: {
                return base(BlockStyle::thematic_break());
            }
            case BK::LinkDefinition: {
                return base(BlockStyle::paragraph());
            }
            case BK::UnsupportedMarkup: {
                auto rb = base(BlockStyle::unsupported());
                rb.raw = b.raw;
                rb.reason_text = unsupported_reason_message(b.unsup_reason);
                return rb;
            }
            case BK::Extension: {
                auto rb = base(BlockStyle::extension());
                rb.extension_name = b.ext_name;
                return rb;
            }
        }
        return base(BlockStyle::paragraph());
    }
};

inline RenderModel build_render_model(const EditorDocument& doc, const Outline& outline) {
    Builder bd;
    std::vector<RenderBlock> blocks;
    blocks.reserve(doc.root.children.size());
    for (const auto& block : doc.root.children) {
        auto rendered = bd.build_block(block);
        rendered.source_fingerprint = block.id.v;
        blocks.push_back(std::move(rendered));
    }
    std::vector<RenderDiagnostic> diags;
    for (const auto& d : doc.diagnostics) diags.push_back(convert_diagnostic(d));
    RenderModel m; m.revision = doc.revision; m.blocks = std::move(blocks);
    m.outline = outline; m.diagnostics = std::move(diags);
    auto collect_editable = [&](auto& self, BlockNode const& block) -> void {
        switch (block.kind) {
            case BlockKind::Paragraph:
            case BlockKind::Heading:
            case BlockKind::TableCell:
            case BlockKind::CodeBlock:
            case BlockKind::MathBlock:
            case BlockKind::ImageBlock:
            case BlockKind::Toc:
            case BlockKind::Frontmatter:
            case BlockKind::ThematicBreak:
            case BlockKind::LinkDefinition:
            case BlockKind::UnsupportedMarkup:
            case BlockKind::Extension:
                m.editable_order.push_back(block.id);
                break;
            default:
                break;
        }
        for (auto const& child : block.children) self(self, child);
    };
    for (auto const& block : doc.root.children) collect_editable(collect_editable, block);
    return m;
}

} // namespace elmd

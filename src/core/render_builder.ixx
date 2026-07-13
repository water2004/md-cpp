// elmd.core.render_builder — derive a local-coordinate RenderModel directly
// from the authoritative block tree and each node's lossless inline source.
export module elmd.core.render_builder;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.selection;
import elmd.core.text_edit;
import elmd.core.document;
import elmd.core.document_text;
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
    if (const auto* document = editable_inline_document(block)) return document->source.size();
    switch (block.kind) {
        case BlockKind::CodeBlock:
        case BlockKind::MathBlock:
            return block.block_source.source.size();
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

// Build a generated marker anchored at a boundary in its owner's source.
inline void push_marker(
    std::vector<InlineRenderItem>& out,
    NodeId owner,
    std::size_t& cursor,
    std::u32string text,
    MarkerRole role,
    TextAffinity boundary_affinity,
    std::u32string display_text = {}) {
    InlineRenderItem m; m.kind = InlineRenderItem::Kind::Marker;
    m.source_span = {owner, {cursor, cursor}};
    m.text = std::move(text); m.display_text = std::move(display_text); m.marker_role = role;
    m.generated_boundary_affinity = boundary_affinity;
    m.marker_style = MarkerStyle{true, {}}; m.visibility = MarkerVisibility::Always;
    out.push_back(std::move(m));
}

struct Builder {
    static bool owns_text_position(const BlockNode& block) {
        if (editable_inline_document(block)) return true;
        switch (block.kind) {
            case BlockKind::CodeBlock:
            case BlockKind::MathBlock:
            case BlockKind::ImageBlock:
            case BlockKind::Toc:
            case BlockKind::Frontmatter:
            case BlockKind::ThematicBreak:
            case BlockKind::LinkDefinition:
            case BlockKind::UnsupportedMarkup:
            case BlockKind::Extension:
                return true;
            default:
                return false;
        }
    }

    std::optional<NodeId> first_editable_owner(const BlockNode& block) const {
        if (owns_text_position(block)) return block.id;
        for (auto const& child : block.children) {
            if (auto owner = first_editable_owner(child)) return owner;
        }
        return std::nullopt;
    }

    std::optional<TextPosition> last_editable_position(const BlockNode& block) const {
        for (auto child = block.children.rbegin(); child != block.children.rend(); ++child) {
            if (auto position = last_editable_position(*child)) return position;
        }
        if (owns_text_position(block)) {
            return TextPosition{block.id, block_local_length(block), TextAffinity::Upstream};
        }
        return std::nullopt;
    }

    void append_block_break(std::vector<InlineRenderItem>& out, NodeId owner, std::size_t source_offset = 0) {
        std::size_t cursor = source_offset;
        push_marker(out, owner, cursor, U"\n", MarkerRole::Structural, TextAffinity::Upstream);
    }
    void append_generated_indent(std::vector<InlineRenderItem>& out, NodeId owner, std::size_t source_offset, std::size_t columns) {
        if (columns == 0) return;
        InlineRenderItem marker;
        marker.kind = InlineRenderItem::Kind::Marker;
        marker.source_span = {owner, {source_offset, source_offset}};
        marker.display_text = std::u32string(columns, U' ');
        marker.marker_role = MarkerRole::Structural;
        marker.generated_boundary_affinity = TextAffinity::Downstream;
        marker.visibility = MarkerVisibility::Always;
        out.push_back(std::move(marker));
    }

    void append_flow_code_contents(
        std::vector<InlineRenderItem>& out,
        const BlockNode& block,
        std::size_t indent_columns,
        std::size_t emitted_columns) {
        constexpr std::size_t content_padding_columns = 2;
        auto append_line_indent = [&](std::size_t source_offset, bool first_line) {
            auto desired = indent_columns + content_padding_columns;
            auto existing = first_line ? emitted_columns : std::size_t{0};
            append_generated_indent(out, block.id, source_offset, desired > existing ? desired - existing : 0);
        };
        const auto code = block_source_content(block.block_source);
        if (code.empty()) {
            append_line_indent(block_source_offset_for_content(block.block_source, 0), true);
            return;
        }
        std::size_t line_start = 0;
        bool first_line = true;
        while (line_start < code.size()) {
            const auto source_start = block_source_offset_for_content(block.block_source, line_start);
            append_line_indent(source_start, first_line);
            auto newline = code.find(U'\n', line_start);
            auto line_end = newline == std::u32string::npos ? code.size() : newline + 1;
            const auto source_end = block_source_offset_for_content(block.block_source, line_end);
            auto item = InlineRenderItem::plain_text(
                code.substr(line_start, line_end - line_start),
                {block.id, {source_start, source_end}});
            item.style.code = true;
            out.push_back(std::move(item));
            line_start = line_end;
            first_line = false;
        }
        if (code.back() == U'\n') {
            append_generated_indent(
                out,
                block.id,
                block_source_offset_for_content(block.block_source, code.size()),
                indent_columns + content_padding_columns);
        }
    }

    struct FlowContext {
        std::size_t indent_columns = 0;
    };

    std::size_t list_marker_columns(const BlockNode& list, std::size_t index) const {
        if (list.kind == BlockKind::TaskList) return 6;
        if (list.list_ordered) return std::to_string(list.list_start + index).size() + 2;
        return 2;
    }

    NodeId list_item_marker_owner(const BlockNode& item) const {
        return first_editable_owner(item).value_or(item.id);
    }

    BlockStyle flow_container_style(BlockKind kind, std::string_view callout_kind = {}) const {
        switch (kind) {
            case BlockKind::BlockQuote: return BlockStyle::blockquote();
            case BlockKind::Callout: return BlockStyle::callout(callout_kind);
            case BlockKind::FootnoteDefinition: return BlockStyle::footnote();
            case BlockKind::List:
            case BlockKind::TaskList: return BlockStyle::list();
            default: return BlockStyle::paragraph();
        }
    }

    RenderBlock make_flow_container(
        const BlockNode& block,
        FlowContext context,
        std::size_t parent_indent_columns) {
        auto rendered = render_block_base(
            block.kind,
            block.id,
            block_local_length(block),
            flow_container_style(block.kind, block.callout_kind));
        rendered.flow_local_indent_columns = context.indent_columns -
            (std::min)(context.indent_columns, parent_indent_columns);
        rendered.flow_anchor_owner_id = first_editable_owner(block).value_or(block.id);
        if (block.kind == BlockKind::Callout) {
            rendered.callout_kind = block.callout_kind;
        } else if (block.kind == BlockKind::FootnoteDefinition) {
            rendered.footnote_label = block.footnote_label;
        }
        return rendered;
    }

    void append_missing_indent(
        std::vector<InlineRenderItem>& out,
        NodeId owner,
        std::size_t source_offset,
        std::size_t desired_columns,
        std::size_t emitted_columns) {
        append_generated_indent(
            out,
            owner,
            source_offset,
            desired_columns > emitted_columns ? desired_columns - emitted_columns : 0);
    }

    RenderBlock append_flow_block(
        std::vector<InlineRenderItem>& out,
        const BlockNode& block,
        FlowContext context,
        std::size_t emitted_columns = 0,
        std::size_t parent_indent_columns = 0) {
        auto owner = first_editable_owner(block).value_or(block.id);
        switch (block.kind) {
            case BlockKind::Document:
            case BlockKind::ListItem:
            case BlockKind::TaskListItem: {
                auto rendered = make_flow_container(block, context, parent_indent_columns);
                for (std::size_t index = 0; index < block.children.size(); ++index) {
                    if (index > 0) {
                        auto previous = last_editable_position(block.children[index - 1])
                            .value_or(TextPosition{owner, 0, TextAffinity::Upstream});
                        append_block_break(out, previous.container_id, previous.source_offset);
                    }
                    rendered.child_blocks.push_back(append_flow_block(
                        out,
                        block.children[index],
                        FlowContext{context.indent_columns},
                        index == 0 ? emitted_columns : 0,
                        context.indent_columns));
                }
                return rendered;
            }
            case BlockKind::List:
            case BlockKind::TaskList: {
                auto rendered = make_flow_container(block, context, parent_indent_columns);
                const bool tasks = block.kind == BlockKind::TaskList;
                for (std::size_t index = 0; index < block.children.size(); ++index) {
                    auto const& item = block.children[index];
                    auto item_owner = list_item_marker_owner(item);
                    if (index > 0) {
                        auto previous = last_editable_position(block.children[index - 1])
                            .value_or(TextPosition{item_owner, 0, TextAffinity::Upstream});
                        append_block_break(out, previous.container_id, previous.source_offset);
                    }
                    auto marker = item.marker;
                    if (marker.empty()) {
                        if (tasks) marker = item.checked ? U"- [x] " : U"- [ ] ";
                        else if (block.list_ordered) marker = utf8_to_cps(std::to_string(block.list_start + index)) + std::u32string(1, block.list_delimiter) + U" ";
                        else marker = U"- ";
                    }
                    auto marker_columns = list_marker_columns(block, index);
                    auto missing_indent = context.indent_columns > (index == 0 ? emitted_columns : 0)
                        ? context.indent_columns - (index == 0 ? emitted_columns : 0)
                        : 0;
                    auto display_marker = tasks
                        ? marker
                        : block.list_ordered
                            ? utf8_to_cps(std::to_string(block.list_start + index)) + std::u32string(1, block.list_delimiter) + U" "
                            : U"\u2022 ";
                    std::size_t cursor = 0;
                    push_marker(
                        out,
                        item_owner,
                        cursor,
                        marker,
                        tasks ? MarkerRole::TaskCheckbox : block.list_ordered ? MarkerRole::ListNumber : MarkerRole::ListBullet,
                        TextAffinity::Downstream,
                        std::u32string(missing_indent, U' ') + display_marker);

                    FlowContext item_context{context.indent_columns + marker_columns};
                    auto item_rendered = make_flow_container(item, item_context, context.indent_columns);
                    for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
                        if (child_index > 0) {
                            auto previous = last_editable_position(item.children[child_index - 1])
                                .value_or(TextPosition{item_owner, 0, TextAffinity::Upstream});
                            append_block_break(out, previous.container_id, previous.source_offset);
                        }
                        item_rendered.child_blocks.push_back(append_flow_block(
                            out,
                            item.children[child_index],
                            item_context,
                            child_index == 0 ? context.indent_columns + marker_columns : 0,
                            item_context.indent_columns));
                    }
                    rendered.child_blocks.push_back(std::move(item_rendered));
                }
                return rendered;
            }
            case BlockKind::BlockQuote:
            case BlockKind::Callout:
            case BlockKind::FootnoteDefinition: {
                auto rendered = make_flow_container(block, context, parent_indent_columns);
                auto content_indent = context.indent_columns + 2;
                std::size_t child_start = 0;
                if (block.kind == BlockKind::Callout && block.callout_title) {
                    append_missing_indent(out, block.id, 0, content_indent, emitted_columns);
                    auto title = build_inline_document(*block.callout_title, block.id, InlineStyle::plain());
                    out.insert(out.end(), title.begin(), title.end());
                    if (!block.children.empty()) {
                        append_block_break(out, block.id, block.callout_title->source.size());
                    }
                    child_start = 1;
                }
                for (std::size_t index = 0; index < block.children.size(); ++index) {
                    if (index > 0) {
                        auto previous = last_editable_position(block.children[index - 1])
                            .value_or(TextPosition{owner, 0, TextAffinity::Upstream});
                        append_block_break(out, previous.container_id, previous.source_offset);
                    }
                    rendered.child_blocks.push_back(append_flow_block(
                        out,
                        block.children[index],
                        FlowContext{content_indent},
                        index == 0 && child_start == 0 ? emitted_columns : 0,
                        context.indent_columns));
                }
                return rendered;
            }
            default:
                break;
        }

        auto rendered = build_block(block);
        switch (block.kind) {
            case BlockKind::Paragraph:
            case BlockKind::Heading:
            case BlockKind::TableCell:
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                out.insert(out.end(), rendered.inline_items.begin(), rendered.inline_items.end());
                break;
            case BlockKind::CodeBlock:
                append_flow_code_contents(out, block, context.indent_columns, emitted_columns);
                break;
            case BlockKind::MathBlock: {
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                const auto math = block_source_content(block.block_source);
                InlineRenderItem item;
                item.kind = InlineRenderItem::Kind::Math;
                item.id = block.id;
                item.source_span = {block.id, {
                    block_source_offset_for_content(block.block_source, 0),
                    block_source_offset_for_content(block.block_source, math.size())}};
                item.source_text = math;
                item.text = math;
                item.display = MathDisplayMode::Block;
                item.math_delim = block.math_delim;
                out.push_back(std::move(item));
                break;
            }
            case BlockKind::ImageBlock: {
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                InlineRenderItem item;
                item.kind = InlineRenderItem::Kind::Image;
                item.id = block.id;
                item.source_span = {block.id, {0, block_local_length(block)}};
                item.src = block.src;
                item.alt = block.image_alt;
                item.title = block.image_title;
                item.image_width = block.image_width;
                item.image_height = block.image_height;
                out.push_back(std::move(item));
                break;
            }
            case BlockKind::Table: {
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                bool first_cell = true;
                for (const auto& row : block.children) {
                    for (const auto& cell : row.children) {
                        if (!first_cell) append_block_break(out, cell.id, 0);
                        auto items = build_inline_document(cell.inline_content, cell.id, InlineStyle::plain());
                        out.insert(out.end(), items.begin(), items.end());
                        first_cell = false;
                    }
                }
                break;
            }
            case BlockKind::Frontmatter:
            case BlockKind::LinkDefinition:
            case BlockKind::UnsupportedMarkup:
            case BlockKind::Extension: {
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                auto raw = utf8_to_cps(block.raw);
                out.push_back(InlineRenderItem::plain_text(raw, {block.id, {0, raw.size()}}));
                break;
            }
            case BlockKind::ThematicBreak:
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                out.push_back(InlineRenderItem::plain_text(U"\u2014", {block.id, {0, block_local_length(block)}}));
                break;
            case BlockKind::Toc:
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                out.push_back(InlineRenderItem::plain_text(U"contents", {block.id, {0, block_local_length(block)}}));
                break;
            default:
                break;
        }
        rendered.flow_local_indent_columns = context.indent_columns -
            (std::min)(context.indent_columns, parent_indent_columns);
        rendered.flow_anchor_owner_id = owner;
        return rendered;
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

    RenderBlock build_block(const BlockNode& b) {
        using BK = BlockKind;
        auto base = [&](BlockStyle style) {
            return render_block_base(b.kind, b.id, block_local_length(b), std::move(style));
        };
        switch (b.kind) {
            case BK::Paragraph:
            case BK::TableCell: {
                auto rb = base(BlockStyle::paragraph());
                if (b.inline_content.source.empty() && b.opening_marker.empty() && b.closing_marker.empty()) {
                    rb.kind = RenderBlockKind::Blank;
                    return rb;
                }
                InlineStyle s = InlineStyle::plain();
                std::size_t cursor = 0;
                if (!b.opening_marker.empty()) {
                    cursor = 0;
                    push_marker(
                        rb.inline_items,
                        b.id,
                        cursor,
                        b.opening_marker,
                        MarkerRole::Syntax,
                        TextAffinity::Downstream);
                    rb.inline_items.back().marker_owner = b.id;
                }
                auto items = build_inline_document(b.inline_content, b.id, s);
                for (auto& item : items) rb.inline_items.push_back(std::move(item));
                if (!b.closing_marker.empty()) {
                    cursor = b.inline_content.source.size();
                    push_marker(
                        rb.inline_items,
                        b.id,
                        cursor,
                        b.closing_marker,
                        MarkerRole::Syntax,
                        TextAffinity::Upstream);
                    rb.inline_items.back().marker_owner = b.id;
                }
                return rb;
            }
            case BK::Heading: {
                auto rb = base(BlockStyle::heading(b.level));
                InlineStyle s = InlineStyle::plain(); s.heading_level = b.level;
                auto append_heading_marker = [&](
                    std::u32string const& text,
                    std::size_t offset,
                    TextAffinity boundary_affinity) {
                    if (text.empty()) return;
                    InlineRenderItem marker;
                    marker.kind = InlineRenderItem::Kind::Marker;
                    marker.source_span = {b.id, {offset, offset}};
                    marker.text = text;
                    marker.style = s;
                    marker.marker_role = MarkerRole::Heading;
                    marker.generated_boundary_affinity = boundary_affinity;
                    marker.marker_owner = b.id;
                    marker.marker_style = MarkerStyle{true, {}};
                    marker.visibility = MarkerVisibility::WhenBlockFocused;
                    rb.inline_items.push_back(std::move(marker));
                };
                append_heading_marker(b.opening_marker, 0, TextAffinity::Downstream);
                auto items = build_inline_document(b.inline_content, b.id, s);
                for (auto& it : items) rb.inline_items.push_back(std::move(it));
                append_heading_marker(
                    b.closing_marker,
                    b.inline_content.source.size(),
                    TextAffinity::Upstream);
                std::stable_sort(rb.inline_items.begin(), rb.inline_items.end(), [](auto const& left, auto const& right) {
                    return left.source_span.source_range.start < right.source_span.source_range.start;
                });
                return rb;
            }
            case BK::BlockQuote:
            case BK::List:
            case BK::TaskList:
            case BK::Callout:
            case BK::FootnoteDefinition: {
                std::vector<InlineRenderItem> flow_items;
                auto rb = append_flow_block(flow_items, b, FlowContext{0});
                rb.inline_items = std::move(flow_items);
                return rb;
            }
            case BK::CodeBlock: {
                auto rb = base(BlockStyle::code());
                rb.raw_source = b.block_source.source;
                rb.content_to_source = b.block_source.tree.content_to_source;
                rb.language = b.block_source.tree.language;
                rb.code_text = block_source_content(b.block_source);
                rb.code_indented = b.code_indented;
                std::size_t n = 1; for (char32_t c : rb.code_text) if (c == '\n') ++n;
                rb.line_count = n;
                if (!rb.content_to_source.empty()) {
                    rb.content_span = {b.id, {rb.content_to_source.front(), rb.content_to_source.back()}};
                }
                return rb;
            }
            case BK::MathBlock: {
                auto rb = base(BlockStyle::math());
                rb.raw_source = b.block_source.source;
                rb.content_to_source = b.block_source.tree.content_to_source;
                rb.tex = block_source_content(b.block_source);
                rb.math_delim = b.math_delim;
                if (!rb.content_to_source.empty()) {
                    rb.content_span = {b.id, {rb.content_to_source.front(), rb.content_to_source.back()}};
                }
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
        blocks.push_back(bd.build_block(block));
    }
    std::vector<RenderDiagnostic> diags;
    for (const auto& d : doc.diagnostics) diags.push_back(convert_diagnostic(d));
    RenderModel m; m.revision = doc.revision; m.blocks = std::move(blocks);
    m.outline = outline; m.diagnostics = std::move(diags);
    auto collect_editable = [&](auto& self, BlockNode const& block) -> void {
        if (Builder::owns_text_position(block)) m.editable_order.push_back(block.id);
        for (auto const& child : block.children) self(self, child);
    };
    for (auto const& block : doc.root.children) collect_editable(collect_editable, block);
    return m;
}

} // namespace elmd

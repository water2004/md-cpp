// elmd.core.render_builder — derive a local-coordinate RenderModel directly
// from the authoritative block tree and each node's lossless inline source.
export module elmd.core.render_builder;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.callout;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.instrumentation;
import elmd.core.selection;
import elmd.core.text_edit;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.symbols;
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

namespace render_key_detail {

struct Hasher {
    std::uint64_t value = 1469598103934665603ull;

    void byte(std::uint8_t input) {
        value ^= input;
        value *= 1099511628211ull;
    }

    template <typename T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    void scalar(T input) {
        if constexpr (std::is_enum_v<T>) {
            scalar(static_cast<std::underlying_type_t<T>>(input));
        } else if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
            byte(input ? 1u : 0u);
        } else {
            using Unsigned = std::make_unsigned_t<T>;
            auto bits = static_cast<Unsigned>(input);
            for (std::size_t index = 0; index < sizeof(bits); ++index) {
                byte(static_cast<std::uint8_t>(bits & 0xffu));
                bits >>= 8;
            }
        }
    }

    void scalar(float input) { scalar(std::bit_cast<std::uint32_t>(input)); }
    void scalar(NodeId input) { scalar(input.v); }
    void scalar(SourceRange input) { scalar(input.start); scalar(input.end); }
    void scalar(TextSpan input) { scalar(input.container_id); scalar(input.source_range); }

    template <typename Character>
    void text(std::basic_string_view<Character> input) {
        scalar(input.size());
        for (auto character : input) scalar(character);
    }

    template <typename Character>
    void text(std::basic_string<Character> const& input) { text(std::basic_string_view<Character>{input}); }

    template <typename T, typename Append>
    void optional(std::optional<T> const& input, Append append) {
        scalar(input.has_value());
        if (input) append(*input);
    }
};

inline void append(Hasher& hash, InlineCstNode const& node) {
    hash.scalar(node.id); hash.scalar(node.kind); hash.scalar(node.range); hash.scalar(node.status);
    const auto& delim = node.delimiter_ranges();
    hash.scalar(delim.full); hash.scalar(delim.opening); hash.scalar(delim.content);
    hash.optional(delim.closing, [&](auto value) { hash.scalar(value); });
    hash.scalar(node.children.size());
    for (auto const& child : node.children) append(hash, child);
}

inline void append(Hasher& hash, InlineDocument const& document) {
    hash.text(document.source);
    hash.scalar(document.tree.nodes.size());
    for (auto const& node : document.tree.nodes) append(hash, node);
}

inline void append(Hasher& hash, BlockNode const& block) {
    hash.scalar(block.id); hash.scalar(block.kind); append(hash, block.inline_content);
    hash.text(block.block_source.source());
    auto const& text = block.text_special();
    hash.scalar(text.level); hash.text(text.slug); hash.text(text.opening_marker); hash.text(text.closing_marker);
    auto const& item = block.item_special();
    hash.text(item.marker); hash.scalar(item.checked);
    auto const& list = block.list_special();
    hash.scalar(list.ordered); hash.scalar(list.start); hash.scalar(list.delimiter);
    auto const& atomic = block.atomic_special();
    hash.scalar(atomic.code_indented); hash.scalar(atomic.math_delim);
    hash.scalar(atomic.toc_marker); hash.scalar(atomic.fmt); hash.text(atomic.raw);
    hash.scalar(atomic.unsup_reason); hash.text(atomic.ext_name);
    auto const& table = block.table_special();
    hash.scalar(table.table_aligns.size());
    for (auto alignment : table.table_aligns) hash.scalar(alignment);
    hash.scalar(table.table_header_row);
    auto const& image = block.image_special();
    hash.text(image.src); hash.text(image.image_alt);
    hash.optional(image.image_title, [&](auto const& value) { hash.text(value); });
    hash.optional(image.image_link, [&](auto const& value) { hash.text(value); });
    hash.optional(image.image_width, [&](auto value) { hash.scalar(value); });
    hash.optional(image.image_height, [&](auto value) { hash.scalar(value); });
    auto const& container = block.container_special();
    hash.text(container.callout_kind); hash.text(container.footnote_label);
    hash.scalar(block.children.size());
    for (auto const& child : block.children) append(hash, child);
}

inline std::uint64_t source_key(BlockNode const& block, std::uint64_t document_dependency_key) {
    record_render_source_key_derivation();
    Hasher hash;
    hash.scalar(document_dependency_key);
    append(hash, block);
    return hash.value;
}

} // namespace render_key_detail

inline std::size_t block_local_length(const BlockNode& block) {
    if (const auto* document = editable_inline_document(block)) return document->source.size();
    switch (block.kind) {
        case BlockKind::CodeBlock:
        case BlockKind::MathBlock:
            return block.block_source.source().size();
        case BlockKind::Frontmatter:
        case BlockKind::UnsupportedMarkup:
        case BlockKind::LinkDefinition:
            return utf8_to_cps(block.atomic_special().raw).size();
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
            case BlockKind::Paragraph: case BlockKind::Heading: case BlockKind::CalloutTitle:
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
            case BlockKind::HtmlContainer: return RenderBlockKind::Text;
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
    m.text = std::move(text);
    m.ensure_special().display_text = std::move(display_text);
    m.ensure_special().marker_role = role;
    m.ensure_special().generated_boundary_affinity = boundary_affinity;
    m.ensure_special().marker_style = MarkerStyle{true, {}};
    m.ensure_special().visibility = MarkerVisibility::Always;
    out.push_back(std::move(m));
}

struct Builder {
    explicit Builder(ThemeProfile const& profile) : theme(profile) {}

    ThemeProfile const& theme;
    std::unordered_map<std::string, std::size_t> footnote_ordinals;

    std::u32string footnote_display_label(std::string_view label) const {
        auto found = footnote_ordinals.find(std::string(label));
        return found == footnote_ordinals.end()
            ? utf8_to_cps(label)
            : utf8_to_cps(std::to_string(found->second));
    }

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
        marker.ensure_special().display_text = std::u32string(columns, U' ');
        marker.ensure_special().marker_role = MarkerRole::Structural;
        marker.ensure_special().generated_boundary_affinity = TextAffinity::Downstream;
        marker.ensure_special().visibility = MarkerVisibility::Always;
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
        const auto lines = code_presentation_lines(code);
        bool first_line = true;
        for (const auto& line : lines) {
            const auto source_start = block_source_offset_for_content(
                block.block_source,
                line.range_with_ending.start);
            append_line_indent(source_start, first_line);
            const auto source_end = block_source_offset_for_content(
                block.block_source,
                line.range_with_ending.end);
            auto item = InlineRenderItem::plain_text(
                code.substr(
                    line.range_with_ending.start,
                    line.range_with_ending.length()),
                {block.id, {source_start, source_end}});
            item.style.code = true;
            out.push_back(std::move(item));
            first_line = false;
        }
    }

    struct FlowContext {
        std::size_t indent_columns = 0;
    };

    std::size_t list_marker_columns(const BlockNode& list, std::size_t index) const {
        if (list.kind == BlockKind::TaskList) return 6;
        if (list.list_special().ordered) return std::to_string(list.list_special().start + index).size() + 2;
        return 2;
    }

    NodeId list_item_marker_owner(const BlockNode& item) const {
        return first_editable_owner(item).value_or(item.id);
    }

    BlockStyle flow_container_style(BlockKind kind, std::string_view callout_kind = {}) const {
        switch (kind) {
            case BlockKind::BlockQuote: return BlockStyle::blockquote(theme.layout);
            case BlockKind::Callout: return BlockStyle::callout(callout_kind, theme.layout);
            case BlockKind::FootnoteDefinition: return BlockStyle::footnote(theme.layout);
            case BlockKind::List:
            case BlockKind::TaskList: return BlockStyle::list(theme.layout);
            default: return BlockStyle::paragraph(theme.layout);
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
            flow_container_style(block.kind, block.container_special().callout_kind));
        rendered.flow_local_indent_columns = context.indent_columns -
            (std::min)(context.indent_columns, parent_indent_columns);
        rendered.flow_anchor_owner_id = first_editable_owner(block).value_or(block.id);
        if (block.kind == BlockKind::Callout) {
            rendered.ensure_special().callout_kind = block.container_special().callout_kind;
        } else if (block.kind == BlockKind::FootnoteDefinition) {
            rendered.ensure_special().footnote_label = block.container_special().footnote_label;
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
            case BlockKind::HtmlContainer:
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
                    auto marker = item.item_special().marker;
                    if (marker.empty()) {
                        if (tasks) marker = item.item_special().checked ? U"- [x] " : U"- [ ] ";
                        else if (block.list_special().ordered) marker = utf8_to_cps(std::to_string(block.list_special().start + index)) + std::u32string(1, block.list_special().delimiter) + U" ";
                        else marker = U"- ";
                    }
                    auto marker_columns = list_marker_columns(block, index);
                    auto missing_indent = context.indent_columns > (index == 0 ? emitted_columns : 0)
                        ? context.indent_columns - (index == 0 ? emitted_columns : 0)
                        : 0;
                    auto display_marker = tasks
                        ? marker
                        : block.list_special().ordered
                            ? utf8_to_cps(std::to_string(block.list_special().start + index)) + std::u32string(1, block.list_special().delimiter) + U" "
                            : U"\u2022 ";
                    std::size_t cursor = 0;
                    push_marker(
                        out,
                        item_owner,
                        cursor,
                        marker,
                        tasks ? MarkerRole::TaskCheckbox : block.list_special().ordered ? MarkerRole::ListNumber : MarkerRole::ListBullet,
                        TextAffinity::Downstream,
                        std::u32string(missing_indent, U' ') + display_marker);
                    if (tasks) out.back().ensure_special().task_checked = item.item_special().checked;

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
                std::u32string footnote_marker;
                std::u32string callout_label;
                if (block.kind == BlockKind::FootnoteDefinition && !block.container_special().footnote_label.empty()) {
                    footnote_marker = footnote_display_label(block.container_special().footnote_label) + U". ";
                }
                if (block.kind == BlockKind::Callout) {
                    callout_label = callout_display_label(block.container_special().callout_kind);
                }
                auto const content_indent = context.indent_columns
                    + (footnote_marker.empty() ? 2u : footnote_marker.size());
                if (!footnote_marker.empty()) {
                    append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                    InlineRenderItem label;
                    label.kind = InlineRenderItem::Kind::Marker;
                    label.source_span = {owner, {0, 0}};
                    label.ensure_special().display_text = footnote_marker;
                    label.ensure_special().ensure_semantic().footnote_label = block.container_special().footnote_label;
                    label.ensure_special().marker_role = MarkerRole::FootnoteLabel;
                    label.ensure_special().generated_boundary_affinity = TextAffinity::Downstream;
                    label.ensure_special().visibility = MarkerVisibility::Always;
                    out.push_back(std::move(label));
                }
                if (!callout_label.empty()) {
                    append_missing_indent(out, owner, 0, content_indent, emitted_columns);
                    InlineRenderItem label;
                    label.kind = InlineRenderItem::Kind::Marker;
                    label.source_span = {owner, {0, 0}};
                    label.ensure_special().display_text = callout_label;
                    label.style.bold = true;
                    label.ensure_special().marker_role = MarkerRole::Structural;
                    label.ensure_special().generated_boundary_affinity = TextAffinity::Downstream;
                    label.ensure_special().visibility = MarkerVisibility::Always;
                    out.push_back(std::move(label));
                    if (!block.children.empty()) {
                        append_block_break(out, owner, 0);
                    }
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
                        index == 0 && callout_label.empty()
                            ? !footnote_marker.empty()
                                ? context.indent_columns + footnote_marker.size()
                                : emitted_columns
                            : 0,
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
            case BlockKind::CalloutTitle:
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
                item.ensure_special().ensure_semantic().source_text = math;
                item.text = math;
                item.ensure_special().display = MathDisplayMode::Block;
                item.ensure_special().math_delim = block.atomic_special().math_delim;
                out.push_back(std::move(item));
                break;
            }
            case BlockKind::ImageBlock: {
                append_missing_indent(out, owner, 0, context.indent_columns, emitted_columns);
                InlineRenderItem item;
                item.kind = InlineRenderItem::Kind::Image;
                item.id = block.id;
                item.source_span = {block.id, {0, block_local_length(block)}};
                auto& semantic = item.ensure_special().ensure_semantic();
                semantic.src = block.image_special().src;
                semantic.alt = block.image_special().image_alt;
                semantic.title = block.image_special().image_title;
                semantic.image_width = block.image_special().image_width;
                semantic.image_height = block.image_special().image_height;
                semantic.block_image = true;
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
                auto raw = utf8_to_cps(block.atomic_special().raw);
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
        const InlineStyle& base,
        bool preserve_soft_breaks = false) {
        std::vector<InlineRenderItem> output;
        auto source_span = [&](SourceRange range) {
            return TextSpan{owner_id, range};
        };
        auto append_marker = [&](std::vector<InlineRenderItem>& target, const InlineCstNode& owner, SourceRange range) {
            if (range.empty()) return;
            InlineRenderItem marker;
            marker.kind = InlineRenderItem::Kind::Marker;
            marker.id = owner.id;
            marker.ensure_special().marker_owner = owner.id;
            marker.source_span = source_span(range);
            marker.text = inline_source_slice(document, range);
            marker.ensure_special().marker_style = MarkerStyle{true, {}};
            marker.ensure_special().visibility = MarkerVisibility::Always;
            target.push_back(std::move(marker));
        };
        std::function<void(const InlineCstNodes&, const InlineStyle&, std::vector<InlineRenderItem>&)> append_nodes;
        append_nodes = [&](const InlineCstNodes& nodes, const InlineStyle& style, std::vector<InlineRenderItem>& target) {
            for (const auto& node : nodes) {
                using K = InlineCstKind;
                const auto& delim = node.delimiter_ranges();
                const auto& semantic = node.semantics();
                auto append_text = [&](std::u32string text, SourceRange range, InlineStyle text_style = InlineStyle::plain()) {
                    InlineRenderItem item = InlineRenderItem::plain_text(std::move(text), source_span(range));
                    item.id = node.id;
                    item.style = text_style;
                    target.push_back(std::move(item));
                };
                switch (node.kind) {
                    case K::Strong: {
                        auto child_style = style; child_style.bold = true;
                        append_marker(target, node, delim.opening);
                        append_nodes(node.children, child_style, target);
                        if (delim.closing) append_marker(target, node, *delim.closing);
                        break;
                    }
                    case K::Emphasis: {
                        auto child_style = style; child_style.italic = true;
                        append_marker(target, node, delim.opening);
                        append_nodes(node.children, child_style, target);
                        if (delim.closing) append_marker(target, node, *delim.closing);
                        break;
                    }
                    case K::Strikethrough: {
                        auto child_style = style; child_style.strikethrough = true;
                        append_marker(target, node, delim.opening);
                        append_nodes(node.children, child_style, target);
                        if (delim.closing) append_marker(target, node, *delim.closing);
                        break;
                    }
                    case K::CodeSpan: {
                        auto child_style = style; child_style.code = true;
                        append_marker(target, node, delim.opening);
                        append_text(inline_source_slice(document, delim.content), delim.content, child_style);
                        if (delim.closing) append_marker(target, node, *delim.closing);
                        break;
                    }
                    case K::InlineMath: {
                        InlineRenderItem item;
                        item.kind = InlineRenderItem::Kind::Math;
                        item.id = node.id;
                        item.source_span = source_span(node.range);
                        item.text = inline_source_slice(document, delim.content);
                        item.ensure_special().display = MathDisplayMode::Inline;
                        item.ensure_special().math_delim = semantic.math_delim;
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
                        auto& special = item.ensure_special().ensure_semantic();
                        special.href = semantic.href;
                        special.title = semantic.title;
                        auto child_style = style; child_style.link = true;
                        if (node.kind == K::Link) {
                            append_marker(special.children, node, delim.opening);
                            append_nodes(node.children, child_style, special.children);
                            if (delim.closing) append_marker(special.children, node, *delim.closing);
                        } else {
                            append_marker(special.children, node, delim.opening);
                            InlineRenderItem text_item = InlineRenderItem::plain_text(
                                inline_source_slice(document, delim.content), source_span(delim.content));
                            text_item.style = child_style;
                            special.children.push_back(std::move(text_item));
                            if (delim.closing) append_marker(special.children, node, *delim.closing);
                        }
                        target.push_back(std::move(item));
                        break;
                    }
                    case K::Image: {
                        InlineRenderItem item;
                        item.kind = InlineRenderItem::Kind::Image;
                        item.id = node.id;
                        item.source_span = source_span(node.range);
                        auto& special = item.ensure_special().ensure_semantic();
                        special.src = semantic.href;
                        special.alt = semantic.alt;
                        special.title = semantic.title;
                        special.image_width = semantic.image_width;
                        special.image_height = semantic.image_height;
                        target.push_back(std::move(item));
                        break;
                    }
                    case K::HtmlElement: {
                        auto child_style = style;
                        if (semantic.html_tag == "u") child_style.underline = true;
                        if (semantic.html_tag == "kbd" || semantic.html_tag == "samp") {
                            child_style.code = true;
                        }
                        if (semantic.html_tag == "var") child_style.italic = true;
                        append_marker(target, node, delim.opening);
                        append_nodes(node.children, child_style, target);
                        if (delim.closing) append_marker(target, node, *delim.closing);
                        break;
                    }
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
                        append_text(preserve_soft_breaks ? U"\n" : U" ", node.range, style);
                        break;
                    case K::HardBreak:
                        append_text(U"\n", node.range, style);
                        break;
                    case K::FootnoteRef: {
                        InlineRenderItem item;
                        item.kind = InlineRenderItem::Kind::FootnoteReference;
                        item.id = node.id;
                        item.source_span = source_span(node.range);
                        item.text = utf8_to_cps(semantic.label);
                        item.ensure_special().display_text = footnote_display_label(semantic.label);
                        item.ensure_special().ensure_semantic().footnote_label = semantic.label;
                        target.push_back(std::move(item));
                        break;
                    }
                    case K::WikiLink:
                        append_text(utf8_to_cps(semantic.alias.value_or(semantic.target)), node.range, InlineStyle{.link = true});
                        break;
                    case K::Incomplete:
                        append_marker(target, node, delim.opening);
                        if (!delim.content.empty()) append_text(inline_source_slice(document, delim.content), delim.content, style);
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
                if (item.kind != InlineRenderItem::Kind::Text
                    && item.kind != InlineRenderItem::Kind::Marker) {
                    item.ensure_special().ensure_semantic().source_text =
                        inline_source_slice(document, item.source_span.source_range);
                }
                if (item.payload && item.payload->semantic_payload) {
                    self(self, item.payload->semantic_payload->children);
                }
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
            case BK::CalloutTitle:
            case BK::TableCell: {
                auto rb = base(BlockStyle::paragraph(theme.layout));
                if (b.inline_content.source.empty() && b.text_special().opening_marker.empty() && b.text_special().closing_marker.empty()) {
                    rb.kind = RenderBlockKind::Blank;
                    return rb;
                }
                InlineStyle s = InlineStyle::plain();
                std::size_t cursor = 0;
                if (!b.text_special().opening_marker.empty()) {
                    cursor = 0;
                    push_marker(
                        rb.inline_items,
                        b.id,
                        cursor,
                        b.text_special().opening_marker,
                        MarkerRole::Syntax,
                        TextAffinity::Downstream);
                    rb.inline_items.back().ensure_special().marker_owner = b.id;
                }
                auto items = build_inline_document(b.inline_content, b.id, s);
                for (auto& item : items) rb.inline_items.push_back(std::move(item));
                if (!b.text_special().closing_marker.empty()) {
                    cursor = b.inline_content.source.size();
                    push_marker(
                        rb.inline_items,
                        b.id,
                        cursor,
                        b.text_special().closing_marker,
                        MarkerRole::Syntax,
                        TextAffinity::Upstream);
                    rb.inline_items.back().ensure_special().marker_owner = b.id;
                }
                return rb;
            }
            case BK::Heading: {
                auto rb = base(BlockStyle::heading(b.text_special().level, theme.layout));
                InlineStyle s = InlineStyle::plain(); s.heading_level = b.text_special().level;
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
                    marker.ensure_special().marker_role = MarkerRole::Heading;
                    marker.ensure_special().generated_boundary_affinity = boundary_affinity;
                    marker.ensure_special().marker_owner = b.id;
                    marker.ensure_special().marker_style = MarkerStyle{true, {}};
                    marker.ensure_special().visibility = MarkerVisibility::WhenBlockFocused;
                    rb.inline_items.push_back(std::move(marker));
                };
                append_heading_marker(b.text_special().opening_marker, 0, TextAffinity::Downstream);
                auto items = build_inline_document(b.inline_content, b.id, s, true);
                for (auto& it : items) rb.inline_items.push_back(std::move(it));
                append_heading_marker(
                    b.text_special().closing_marker,
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
            case BK::HtmlContainer: {
                std::vector<InlineRenderItem> flow_items;
                auto rb = append_flow_block(flow_items, b, FlowContext{0});
                rb.inline_items = std::move(flow_items);
                return rb;
            }
            case BK::CodeBlock: {
                auto rb = base(BlockStyle::code(theme.layout));
                auto& special = rb.ensure_special();
                special.raw_source = b.block_source.source();
                special.content_to_source = b.block_source.tree().content_to_source;
                special.language = b.block_source.tree().language;
                special.code_text = block_source_content(b.block_source);
                special.code_indented = b.atomic_special().code_indented;
                special.line_count = code_presentation_lines(special.code_text).size();
                if (!special.content_to_source.empty()) {
                    rb.content_span = {b.id, {special.content_to_source.front(), special.content_to_source.back()}};
                }
                return rb;
            }
            case BK::MathBlock: {
                auto rb = base(BlockStyle::math(theme.layout));
                auto& special = rb.ensure_special();
                special.raw_source = b.block_source.source();
                special.content_to_source = b.block_source.tree().content_to_source;
                special.tex = block_source_content(b.block_source);
                special.math_delim = b.atomic_special().math_delim;
                if (!special.content_to_source.empty()) {
                    rb.content_span = {b.id, {special.content_to_source.front(), special.content_to_source.back()}};
                }
                return rb;
            }
            case BK::Table: {
                auto rb = base(BlockStyle::table(theme.layout));
                auto& special = rb.ensure_special();
                special.table_aligns = b.table_special().table_aligns;
                special.table_header_row = !b.children.empty() && b.children.front().table_special().table_header_row;
                special.column_count = b.children.empty() ? 0 : b.children.front().children.size();
                special.row_count = b.children.size();
                auto build_cell = [&](const BlockNode& cell) {
                    special.table_cell_spans.push_back({cell.id, {0, cell.inline_content.source.size()}});
                    return build_inline_document(cell.inline_content, cell.id, InlineStyle::plain());
                };
                for (const auto& row : b.children) for (const auto& cell : row.children) special.table_cells.push_back(build_cell(cell));
                return rb;
            }
            case BK::ImageBlock: {
                auto rb = base(BlockStyle::image(theme.layout));
                auto& special = rb.ensure_special();
                special.alt = b.image_special().image_alt; special.src = b.image_special().src; special.title = b.image_special().image_title; special.link = b.image_special().image_link;
                special.image_width = b.image_special().image_width; special.image_height = b.image_special().image_height;
                return rb;
            }
            case BK::Toc: {
                return base(BlockStyle::toc(theme.layout));
            }
            case BK::Frontmatter: {
                auto rb = base(BlockStyle::frontmatter(theme.layout));
                rb.ensure_special().raw = b.atomic_special().raw;
                return rb;
            }
            case BK::ThematicBreak: {
                return base(BlockStyle::thematic_break(theme.layout));
            }
            case BK::LinkDefinition: {
                return base(BlockStyle::paragraph(theme.layout));
            }
            case BK::UnsupportedMarkup: {
                auto rb = base(BlockStyle::unsupported(theme.layout));
                auto& special = rb.ensure_special();
                special.raw = b.atomic_special().raw;
                special.reason_text = unsupported_reason_message(b.atomic_special().unsup_reason);
                return rb;
            }
            case BK::Extension: {
                auto rb = base(BlockStyle::extension(theme.layout));
                rb.ensure_special().extension_name = b.atomic_special().ext_name;
                return rb;
            }
        }
        return base(BlockStyle::paragraph(theme.layout));
    }

    RenderBlock build_block_skeleton(const BlockNode& b) {
        using BK = BlockKind;
        auto style = [&]() {
            switch (b.kind) {
                case BK::Heading: return BlockStyle::heading(b.text_special().level, theme.layout);
                case BK::BlockQuote: return BlockStyle::blockquote(theme.layout);
                case BK::List:
                case BK::TaskList: return BlockStyle::list(theme.layout);
                case BK::CodeBlock: return BlockStyle::code(theme.layout);
                case BK::MathBlock: return BlockStyle::math(theme.layout);
                case BK::Table: return BlockStyle::table(theme.layout);
                case BK::ImageBlock: return BlockStyle::image(theme.layout);
                case BK::Toc: return BlockStyle::toc(theme.layout);
                case BK::Callout: return BlockStyle::callout(b.container_special().callout_kind, theme.layout);
                case BK::FootnoteDefinition: return BlockStyle::footnote(theme.layout);
                case BK::Frontmatter: return BlockStyle::frontmatter(theme.layout);
                case BK::ThematicBreak: return BlockStyle::thematic_break(theme.layout);
                case BK::UnsupportedMarkup: return BlockStyle::unsupported(theme.layout);
                case BK::Extension: return BlockStyle::extension(theme.layout);
                default: return BlockStyle::paragraph(theme.layout);
            }
        }();
        auto rendered = render_block_base(b.kind, b.id, block_local_length(b), std::move(style));
        rendered.materialized = false;
        if ((b.kind == BK::Paragraph || b.kind == BK::CalloutTitle || b.kind == BK::TableCell)
            && b.inline_content.source.empty()
            && b.text_special().opening_marker.empty()
            && b.text_special().closing_marker.empty()) {
            rendered.kind = RenderBlockKind::Blank;
        }
        if (b.kind == BK::Heading) rendered.text_heading_level = b.text_special().level;
        if (b.kind == BK::CodeBlock || b.kind == BK::MathBlock) {
            auto const& offsets = b.block_source.tree().content_to_source;
            if (!offsets.empty()) {
                rendered.content_span = {b.id, {offsets.front(), offsets.back()}};
            }
        }
        if (b.kind == BK::ImageBlock) {
            auto& special = rendered.ensure_special();
            special.src = b.image_special().src;
            special.alt = b.image_special().image_alt;
            special.image_width = b.image_special().image_width;
            special.image_height = b.image_special().image_height;
        }

        std::uint64_t characters = 0;
        std::uint64_t line_breaks = 0;
        auto count_text = [&](auto const& text) {
            characters += text.size();
            line_breaks += static_cast<std::uint64_t>(std::ranges::count(text, U'\n'));
        };
        auto visit = [&](auto& self, BlockNode const& block) -> void {
            if (auto const* inline_document = editable_inline_document(block)) {
                count_text(inline_document->source);
                characters += block.text_special().opening_marker.size();
                characters += block.text_special().closing_marker.size();
            } else if (block.kind == BK::CodeBlock || block.kind == BK::MathBlock) {
                count_text(block.block_source.tree().content);
            } else if (block.kind == BK::Frontmatter
                || block.kind == BK::UnsupportedMarkup
                || block.kind == BK::LinkDefinition) {
                characters += block.atomic_special().raw.size();
                line_breaks += static_cast<std::uint64_t>(
                    std::ranges::count(block.atomic_special().raw, '\n'));
            } else if (block.kind == BK::ImageBlock) {
                characters += block.image_special().image_alt.size();
            }
            characters += block.item_special().marker.size();
            for (auto const& child : block.children) self(self, child);
        };
        visit(visit, b);
        if (b.kind == BK::Table) {
            // The height estimator treats this field as a row-count fallback
            // until the full table projection is materialized.
            line_breaks = (std::max)(line_breaks, static_cast<std::uint64_t>(b.children.size()));
        }
        rendered.estimated_characters = static_cast<std::uint32_t>((std::min)(
            characters,
            static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
        rendered.estimated_line_breaks = static_cast<std::uint32_t>((std::min)(
            line_breaks,
            static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
        return rendered;
    }
};

inline std::uint64_t configure_render_dependencies(
    Builder& builder,
    DocumentSymbolIndex const& symbols) {
    std::size_t next_footnote_ordinal = 1;
    render_key_detail::Hasher dependency_hash;
    auto assign_footnote_ordinal = [&](std::string const& label) {
        if (!builder.footnote_ordinals.contains(label)) {
            auto ordinal = next_footnote_ordinal++;
            builder.footnote_ordinals.emplace(label, ordinal);
            dependency_hash.text(label);
            dependency_hash.scalar(ordinal);
        }
    };
    // Number by first reference, as rendered Markdown does. Keep unreferenced
    // definitions visible and stable by assigning their numbers afterwards in
    // source order; their source labels remain authoritative and unchanged.
    for (auto const& reference : symbols.footnote_references) {
        assign_footnote_ordinal(reference.label);
    }
    for (auto const& definition : symbols.footnotes) {
        assign_footnote_ordinal(definition.label);
    }
    return dependency_hash.value;
}

inline void update_render_geometry_hints(RenderBlock& block) {
    std::uint64_t characters = 0;
    std::uint64_t line_breaks = 0;
    std::vector<std::string> image_sources;
    std::optional<std::uint8_t> homogeneous_heading_level;
    bool saw_visible_text = false;
    bool mixed_heading_styles = false;
    auto visit = [&](auto& self, std::vector<InlineRenderItem> const& items) -> void {
        for (auto const& item : items) {
            auto const& text = item.special().display_text.empty()
                ? item.text : item.special().display_text;
            if (!text.empty()) {
                saw_visible_text = true;
                if (!item.style.heading_level) {
                    mixed_heading_styles = true;
                } else if (!homogeneous_heading_level) {
                    homogeneous_heading_level = *item.style.heading_level;
                } else if (*homogeneous_heading_level != *item.style.heading_level) {
                    mixed_heading_styles = true;
                }
            }
            characters += text.size();
            line_breaks += static_cast<std::uint64_t>(std::ranges::count(text, U'\n'));
            if (item.kind == InlineRenderItem::Kind::Image && !item.special().semantic().src.empty()) {
                image_sources.push_back(item.special().semantic().src);
            }
            self(self, item.special().semantic().children);
        }
    };
    visit(visit, block.inline_items);
    if (saw_visible_text) {
        block.text_heading_level = homogeneous_heading_level && !mixed_heading_styles
            ? *homogeneous_heading_level
            : 0;
    }
    block.estimated_characters = static_cast<std::uint32_t>((std::min)(
        characters,
        static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
    block.estimated_line_breaks = static_cast<std::uint32_t>((std::min)(
        line_breaks,
        static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
    if (!image_sources.empty())
        block.ensure_special().inline_image_sources = std::move(image_sources);
}

inline RenderModel finish_render_model(
    const EditorDocument& doc,
    const Outline& outline,
    std::vector<RenderBlock> blocks,
    std::optional<std::tuple<
        std::vector<NodeId>,
        std::unordered_map<std::uint64_t, std::size_t>,
        std::unordered_map<std::uint64_t, NodeId>>> cached_editable = std::nullopt) {
    std::vector<RenderDiagnostic> diags;
    for (const auto& d : doc.diagnostics) diags.push_back(convert_diagnostic(d));
    RenderModel m; m.revision = doc.revision; m.blocks = std::move(blocks);
    m.outline = outline; m.diagnostics = std::move(diags);
    if (cached_editable) {
        m.editable_order = std::move(std::get<0>(*cached_editable));
        m.editable_index = std::move(std::get<1>(*cached_editable));
        m.editable_top_level = std::move(std::get<2>(*cached_editable));
        return m;
    }
    m.editable_order = doc.cached_editable_order;
    m.editable_index = doc.cached_editable_index;
    m.editable_top_level.reserve(m.editable_order.size());
    for (auto owner : m.editable_order) {
        if (auto top_level = document_top_level_block_id(doc, owner)) {
            m.editable_top_level.emplace(owner.v, *top_level);
        }
    }
    return m;
}

inline RenderModel build_render_model(
    const EditorDocument& doc,
    const Outline& outline,
    DocumentSymbolIndex const& symbols,
    ThemeProfile const& theme) {
    Builder builder(theme);
    auto dependency_key = configure_render_dependencies(builder, symbols);
    std::vector<RenderBlock> blocks;
    blocks.reserve(doc.root.children.size());
    for (const auto& block : doc.root.children) {
        auto rendered = builder.build_block(block);
        update_render_geometry_hints(rendered);
        rendered.source_key = render_key_detail::source_key(block, dependency_key);
        rendered.presentation_key = rendered.source_key;
        blocks.push_back(std::move(rendered));
    }
    auto model = finish_render_model(doc, outline, std::move(blocks));
    model.document_dependency_key = dependency_key;
    model.rebuilt_block_count = model.blocks.size();
    return model;
}

inline std::uint64_t render_skeleton_key(
    NodeId id,
    std::uint64_t document_dependency_key) {
    render_key_detail::Hasher hash;
    hash.scalar(document_dependency_key);
    hash.scalar(id);
    return hash.value;
}

inline RenderModel build_virtualized_render_model(
    const EditorDocument& doc,
    const Outline& outline,
    DocumentSymbolIndex const& symbols,
    ThemeProfile const& theme) {
    Builder builder(theme);
    auto dependency_key = configure_render_dependencies(builder, symbols);
    std::vector<RenderBlock> blocks;
    blocks.reserve(doc.root.children.size());
    for (auto const& block : doc.root.children) {
        auto rendered = builder.build_block_skeleton(block);
        rendered.presentation_key = render_skeleton_key(block.id, dependency_key);
        blocks.push_back(std::move(rendered));
    }
    auto model = finish_render_model(doc, outline, std::move(blocks));
    model.document_dependency_key = dependency_key;
    model.virtualized = true;
    model.reused_block_count = model.blocks.size();
    return model;
}

inline void materialize_render_model_range(
    RenderModel& model,
    const EditorDocument& doc,
    DocumentSymbolIndex const& symbols,
    ThemeProfile const& theme,
    std::size_t begin,
    std::size_t end) {
    if (!model.virtualized || model.blocks.empty()) return;
    begin = (std::min)(begin, model.blocks.size());
    end = (std::min)((std::max)(begin, end), model.blocks.size());
    Builder builder(theme);
    auto dependency_key = configure_render_dependencies(builder, symbols);
    for (auto index = begin; index < end; ++index) {
        if (model.blocks[index].materialized) continue;
        auto const& source = doc.root.children[index];
        auto rendered = builder.build_block(source);
        update_render_geometry_hints(rendered);
        rendered.source_key = render_key_detail::source_key(source, dependency_key);
        rendered.presentation_key = rendered.source_key;
        model.blocks[index] = std::move(rendered);
        model.materialized_block_indices.insert(index);
    }
}

inline void release_render_model_blocks_outside(
    RenderModel& model,
    const EditorDocument& doc,
    ThemeProfile const& theme,
    std::size_t retain_begin,
    std::size_t retain_end) {
    if (!model.virtualized || model.materialized_block_indices.empty()) return;
    retain_begin = (std::min)(retain_begin, model.blocks.size());
    retain_end = (std::min)((std::max)(retain_begin, retain_end), model.blocks.size());
    Builder builder(theme);
    auto active = std::vector<std::size_t>(
        model.materialized_block_indices.begin(),
        model.materialized_block_indices.end());
    for (auto index : active) {
        if (index >= retain_begin && index < retain_end) continue;
        if (index >= doc.root.children.size()) {
            model.materialized_block_indices.erase(index);
            continue;
        }
        auto skeleton = builder.build_block_skeleton(doc.root.children[index]);
        skeleton.presentation_key = render_skeleton_key(
            skeleton.id,
            model.document_dependency_key);
        model.blocks[index] = std::move(skeleton);
        model.materialized_block_indices.erase(index);
    }
}

inline RenderModel build_render_model_incremental(
    const EditorDocument& doc,
    const Outline& outline,
    DocumentSymbolIndex const& symbols,
    ThemeProfile const& theme,
    RenderModel previous,
    RenderModelUpdate const& update) {
    Builder builder(theme);
    auto dependency_key = configure_render_dependencies(builder, symbols);
    bool top_level_identity_unchanged = previous.blocks.size() == doc.root.children.size();
    if (top_level_identity_unchanged) {
        for (std::size_t index = 0; index < previous.blocks.size(); ++index) {
            if (previous.blocks[index].id != doc.root.children[index].id) {
                top_level_identity_unchanged = false;
                break;
            }
        }
    }
    const bool structure_unchanged = !update.structural && top_level_identity_unchanged;

    std::unordered_set<std::uint64_t> changed_top_levels;
    bool local_invalidation = structure_unchanged
        && dependency_key == previous.document_dependency_key;
    if (local_invalidation) {
        for (auto owner : update.changed_owners) {
            auto found = previous.editable_top_level.find(owner.v);
            if (found == previous.editable_top_level.end()) {
                local_invalidation = false;
                break;
            }
            changed_top_levels.insert(found->second.v);
        }
    }

    if (local_invalidation) {
        std::vector<std::size_t> changed_indices;
        changed_indices.reserve(changed_top_levels.size());
        for (auto top_level : changed_top_levels) {
            auto path = document_block_path(doc, NodeId{top_level});
            if (!path || path->size() != 1 || path->front() >= previous.blocks.size()) {
                local_invalidation = false;
                break;
            }
            changed_indices.push_back(path->front());
        }
        if (local_invalidation) {
            std::ranges::sort(changed_indices);
            changed_indices.erase(
                std::unique(changed_indices.begin(), changed_indices.end()),
                changed_indices.end());
            for (auto index : changed_indices) {
                auto const& block = doc.root.children[index];
                auto rendered = builder.build_block(block);
                update_render_geometry_hints(rendered);
                rendered.source_key = render_key_detail::source_key(block, dependency_key);
                rendered.presentation_key = rendered.source_key;
                previous.blocks[index] = std::move(rendered);
                if (previous.virtualized) previous.materialized_block_indices.insert(index);
            }
            previous.revision = doc.revision;
            if (previous.outline.content_key == outline.content_key) {
                previous.outline.revision = outline.revision;
                previous.outline.content_revision = outline.content_revision;
            } else {
                previous.outline = outline;
            }
            previous.document_dependency_key = dependency_key;
            previous.rebuilt_block_count = changed_indices.size();
            previous.reused_block_count = previous.blocks.size() - changed_indices.size();
            previous.incremental_update = true;
            previous.changed_block_indices = std::move(changed_indices);
            return previous;
        }
    }

    const bool trusted_structural_locality = update.structural
        && update.structural_locality_known
        && (!update.changed_owners.empty() || !update.structural_anchors.empty())
        && dependency_key == previous.document_dependency_key;
    if (previous.virtualized && update.structural && !trusted_structural_locality) {
        auto model = build_virtualized_render_model(doc, outline, symbols, theme);
        model.rebuilt_block_count = doc.root.children.size();
        return model;
    }
    if (trusted_structural_locality) {
        auto note_top_level = [&](NodeId id) {
            auto path = document_block_path(doc, id);
            if (!path || path->empty() || path->front() >= doc.root.children.size()) return;
            changed_top_levels.insert(doc.root.children[path->front()].id.v);
        };
        for (auto owner : update.changed_owners) note_top_level(owner);
        for (auto anchor : update.structural_anchors) note_top_level(anchor);
    }

    std::unordered_map<std::uint64_t, std::size_t> previous_by_id;
    previous_by_id.reserve(previous.blocks.size());
    for (std::size_t index = 0; index < previous.blocks.size(); ++index) {
        previous_by_id.emplace(previous.blocks[index].id.v, index);
    }
    std::vector<RenderBlock> blocks;
    blocks.reserve(doc.root.children.size());
    std::size_t reused = 0;
    std::size_t rebuilt = 0;
    for (std::size_t index = 0; index < doc.root.children.size(); ++index) {
        auto const& block = doc.root.children[index];
        auto found = previous_by_id.find(block.id.v);
        if (trusted_structural_locality
            && !changed_top_levels.contains(block.id.v)
            && found != previous_by_id.end()
            && !previous.blocks[found->second].source_mode) {
            blocks.push_back(std::move(previous.blocks[found->second]));
            ++reused;
            continue;
        }
        auto source_key = render_key_detail::source_key(block, dependency_key);
        if (found != previous_by_id.end()
            && previous.blocks[found->second].source_key == source_key
            && !previous.blocks[found->second].source_mode) {
            blocks.push_back(std::move(previous.blocks[found->second]));
            ++reused;
            continue;
        }
        auto rendered = builder.build_block(block);
        update_render_geometry_hints(rendered);
        rendered.source_key = source_key;
        rendered.presentation_key = source_key;
        blocks.push_back(std::move(rendered));
        ++rebuilt;
    }
    std::optional<std::tuple<
        std::vector<NodeId>,
        std::unordered_map<std::uint64_t, std::size_t>,
        std::unordered_map<std::uint64_t, NodeId>>> cached_editable;
    if (structure_unchanged) {
        cached_editable.emplace(
            std::move(previous.editable_order),
            std::move(previous.editable_index),
            std::move(previous.editable_top_level));
    }
    auto model = finish_render_model(doc, outline, std::move(blocks), std::move(cached_editable));
    model.document_dependency_key = dependency_key;
    model.rebuilt_block_count = rebuilt;
    model.reused_block_count = reused;
    model.incremental_update = trusted_structural_locality
        && top_level_identity_unchanged;
    model.virtualized = previous.virtualized;
    if (model.virtualized) {
        for (std::size_t index = 0; index < model.blocks.size(); ++index) {
            if (model.blocks[index].materialized) {
                model.materialized_block_indices.insert(index);
            }
        }
    }
    if (model.incremental_update) {
        model.changed_block_indices.reserve(changed_top_levels.size());
        for (std::size_t index = 0; index < doc.root.children.size(); ++index) {
            if (changed_top_levels.contains(doc.root.children[index].id.v)) {
                model.changed_block_indices.push_back(index);
            }
        }
    } else {
        model.changed_block_indices.clear();
    }
    return model;
}

} // namespace elmd

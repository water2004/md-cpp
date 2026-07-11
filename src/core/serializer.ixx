export module elmd.core.serializer;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.utf;

export namespace elmd {

namespace serializer_detail {

inline std::u32string serialize_inlines(const InlineVec& nodes);

inline std::u32string marker_or(const std::u32string& marker, std::u32string_view fallback) {
    return marker.empty() ? std::u32string(fallback) : marker;
}

inline std::u32string serialize_inline(const InlineNode& node) {
    switch (node.kind) {
        case InlineKind::Text:
        case InlineKind::UnsupportedMarkup:
            return node.text;
        case InlineKind::Emphasis:
            return marker_or(node.opening_marker, U"*") + serialize_inlines(node.children) + marker_or(node.closing_marker, U"*");
        case InlineKind::Strong:
            return marker_or(node.opening_marker, U"**") + serialize_inlines(node.children) + marker_or(node.closing_marker, U"**");
        case InlineKind::Strike:
            return marker_or(node.opening_marker, U"~~") + serialize_inlines(node.children) + marker_or(node.closing_marker, U"~~");
        case InlineKind::Span:
            return serialize_inlines(node.children);
        case InlineKind::InlineCode:
            return marker_or(node.opening_marker, U"`") + node.text + marker_or(node.closing_marker, U"`");
        case InlineKind::InlineMath: {
            auto opening = node.math_delim == MathDelimiter::InlineParen ? std::u32string(U"\\(") : std::u32string(U"$");
            auto closing = node.math_delim == MathDelimiter::InlineParen ? std::u32string(U"\\)") : std::u32string(U"$");
            return marker_or(node.opening_marker, opening) + node.text + marker_or(node.closing_marker, closing);
        }
        case InlineKind::Link: {
            auto result = U"[" + serialize_inlines(node.children) + U"](" + utf8_to_cps(node.href);
            if (node.title) result += U" \"" + utf8_to_cps(*node.title) + U"\"";
            result += U")";
            return result;
        }
        case InlineKind::Image: {
            auto result = U"![" + utf8_to_cps(node.alt) + U"](" + utf8_to_cps(node.href);
            if (node.title) result += U" \"" + utf8_to_cps(*node.title) + U"\"";
            result += U")";
            return result;
        }
        case InlineKind::FootnoteRef:
            return U"[^" + utf8_to_cps(node.label) + U"]";
        case InlineKind::WikiLink:
            return node.alias ? U"[[" + utf8_to_cps(node.target) + U"|" + utf8_to_cps(*node.alias) + U"]]"
                              : U"[[" + utf8_to_cps(node.target) + U"]]";
        case InlineKind::SoftBreak:
            return U"\n";
        case InlineKind::HardBreak:
            return U"  \n";
        case InlineKind::Extension:
            return node.ext_text.empty() ? U"[ext:" + utf8_to_cps(node.ext_name) + U"]"
                                         : U"[ext:" + utf8_to_cps(node.ext_name) + U":" + node.ext_text + U"]";
    }
    return {};
}

inline std::u32string serialize_inlines(const InlineVec& nodes) {
    std::u32string result;
    for (const auto& node : nodes) result += serialize_inline(node);
    return result;
}

inline std::vector<std::u32string> lines(std::u32string_view value) {
    std::vector<std::u32string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        auto end = value.find(U'\n', start);
        if (end == std::u32string_view::npos) {
            result.emplace_back(value.substr(start));
            break;
        }
        result.emplace_back(value.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

inline std::u32string serialize_block(const BlockNode& block);
inline std::u32string serialize_blocks(const BlockVec& blocks);

inline std::u32string serialize_list_item_blocks(const BlockVec& blocks) {
    std::u32string result;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (index > 0) {
            const auto nested = blocks[index].kind == BlockKind::List || blocks[index].kind == BlockKind::TaskList;
            result += nested ? U"\n" : U"\n\n";
        }
        result += serialize_block(blocks[index]);
    }
    return result;
}

inline std::u32string indent_continuation(std::u32string_view value, std::size_t width) {
    auto source_lines = lines(value);
    std::u32string result;
    const std::u32string indent(width, U' ');
    for (std::size_t index = 0; index < source_lines.size(); ++index) {
        if (index > 0) result += U"\n" + indent;
        result += source_lines[index];
    }
    return result;
}

inline std::u32string serialize_list(const BlockNode& block) {
    std::u32string result;
    const auto count = block.kind == BlockKind::TaskList ? block.task_items.size() : block.list_items.size();
    for (std::size_t index = 0; index < count; ++index) {
        std::u32string marker;
        const BlockVec* children = nullptr;
        if (block.kind == BlockKind::TaskList) {
            const auto& item = block.task_items[index];
            marker = item.marker.empty() ? (item.checked ? U"- [x] " : U"- [ ] ") : item.marker;
            children = &item.children;
        } else {
            const auto& item = block.list_items[index];
            if (!item.marker.empty()) marker = item.marker;
            else if (block.list_ordered) marker = utf8_to_cps(std::to_string(block.list_start + index)) + std::u32string(1, block.list_delimiter) + U" ";
            else marker = U"- ";
            children = &item.children;
        }
        auto body = serialize_list_item_blocks(*children);
        result += marker + indent_continuation(body, marker.size());
        if (index + 1 < count) result += U"\n";
    }
    return result;
}

inline std::u32string serialize_table_row(const std::vector<TableCell>& cells) {
    std::u32string result = U"|";
    for (const auto& cell : cells) result += U" " + serialize_inlines(cell.children) + U" |";
    return result;
}

inline std::u32string serialize_block(const BlockNode& block) {
    switch (block.kind) {
        case BlockKind::Paragraph:
            return serialize_inlines(block.children);
        case BlockKind::Heading:
            return std::u32string(block.level == 0 ? 1 : block.level, U'#') + U" " + serialize_inlines(block.children);
        case BlockKind::BlockQuote: {
            auto body = serialize_blocks(block.quote_children);
            auto source_lines = lines(body);
            std::u32string result;
            for (std::size_t index = 0; index < source_lines.size(); ++index) {
                if (index > 0) result += U"\n";
                result += source_lines[index].empty() ? U">" : U"> " + source_lines[index];
            }
            return result;
        }
        case BlockKind::List:
        case BlockKind::TaskList:
            return serialize_list(block);
        case BlockKind::CodeBlock:
            if (block.code_indented) return U"    " + indent_continuation(block.code_text, 4);
            return U"```" + (block.language ? utf8_to_cps(*block.language) : std::u32string{}) + U"\n" + block.code_text + U"\n```";
        case BlockKind::MathBlock: {
            if (block.math_delim == MathDelimiter::BlockBracket) return U"\\[\n" + block.tex + U"\n\\]";
            if (block.math_delim == MathDelimiter::FencedMath) return U"```math\n" + block.tex + U"\n```";
            return U"$$\n" + block.tex + U"\n$$";
        }
        case BlockKind::Table: {
            std::u32string result = serialize_table_row(block.table_header) + U"\n|";
            for (std::size_t index = 0; index < block.table_header.size(); ++index) {
                auto alignment = index < block.table_aligns.size() ? block.table_aligns[index] : TableAlignment::None;
                if (alignment == TableAlignment::Left) result += U" :--- |";
                else if (alignment == TableAlignment::Center) result += U" :---: |";
                else if (alignment == TableAlignment::Right) result += U" ---: |";
                else result += U" --- |";
            }
            for (const auto& row : block.table_rows) result += U"\n" + serialize_table_row(row.cells);
            return result;
        }
        case BlockKind::ImageBlock: {
            auto result = U"![" + utf8_to_cps(block.image_alt) + U"](" + utf8_to_cps(block.src);
            if (block.image_title) result += U" \"" + utf8_to_cps(*block.image_title) + U"\"";
            return result + U")";
        }
        case BlockKind::Callout: {
            std::u32string first = U"> [!" + utf8_to_cps(block.callout_kind) + U"]";
            if (block.callout_title) first += U" " + serialize_inlines(*block.callout_title);
            auto body = serialize_blocks(block.quote_children);
            if (body.empty()) return first;
            std::u32string result = first;
            for (const auto& line : lines(body)) result += U"\n" + (line.empty() ? std::u32string(U">") : U"> " + line);
            return result;
        }
        case BlockKind::FootnoteDefinition: {
            auto body = serialize_blocks(block.quote_children);
            return U"[^" + utf8_to_cps(block.footnote_label) + U"]: " + indent_continuation(body, block.footnote_label.size() + 4);
        }
        case BlockKind::Toc:
            return block.toc_marker == TocMarkerKind::WikiToc ? U"[[TOC]]" : U"[TOC]";
        case BlockKind::Frontmatter: {
            std::u32string marker = block.fmt == FrontmatterFormat::Toml ? U"+++" : U"---";
            return marker + U"\n" + utf8_to_cps(block.raw) + U"\n" + marker;
        }
        case BlockKind::ThematicBreak:
            return U"---";
        case BlockKind::LinkDefinition:
        case BlockKind::UnsupportedMarkup:
            return utf8_to_cps(block.raw);
        case BlockKind::Extension:
            return U"[ext:" + utf8_to_cps(block.ext_name) + U"]";
    }
    return {};
}

inline std::u32string serialize_blocks(const BlockVec& blocks) {
    std::u32string result;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (index > 0) result += U"\n\n";
        result += serialize_block(blocks[index]);
    }
    return result;
}

}

inline std::u32string serialize_markdown_cps(const EditorDocument& document) {
    return serializer_detail::serialize_blocks(document.blocks);
}

inline std::string serialize_markdown(const EditorDocument& document) {
    return cps_to_utf8(serialize_markdown_cps(document));
}

}

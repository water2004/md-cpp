export module elmd.core.serializer;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.source_map;
import elmd.core.utf;

export namespace elmd {

struct SerializedDocument {
    std::u32string markdown;
    SourceMap source_map;
};

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

inline CharRange char_range(std::size_t start, std::size_t end) {
    return CharRange(CharOffset(start), CharOffset(end));
}

inline std::size_t find_serialized(
    const std::u32string& markdown,
    std::u32string_view value,
    std::size_t cursor,
    std::size_t limit) {
    if (value.empty()) return (std::min)(cursor, limit);
    const auto found = markdown.find(value, cursor);
    return found == std::u32string::npos || found + value.size() > limit ? (std::min)(cursor, limit) : found;
}

inline std::size_t map_inlines(
    const InlineVec& nodes,
    const std::u32string& markdown,
    std::size_t cursor,
    std::size_t limit,
    SourceMap& source_map);

inline std::size_t map_inline(
    const InlineNode& node,
    const std::u32string& markdown,
    std::size_t cursor,
    std::size_t limit,
    SourceMap& source_map) {
    const bool container = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
        || node.kind == InlineKind::Strike || node.kind == InlineKind::Span || node.kind == InlineKind::Link;
    if (container) {
        std::u32string opening;
        std::u32string closing;
        if (node.kind == InlineKind::Emphasis) {
            opening = marker_or(node.opening_marker, U"*");
            closing = marker_or(node.closing_marker, U"*");
        } else if (node.kind == InlineKind::Strong) {
            opening = marker_or(node.opening_marker, U"**");
            closing = marker_or(node.closing_marker, U"**");
        } else if (node.kind == InlineKind::Strike) {
            opening = marker_or(node.opening_marker, U"~~");
            closing = marker_or(node.closing_marker, U"~~");
        } else if (node.kind == InlineKind::Link) {
            opening = U"[";
            closing = U"](" + utf8_to_cps(node.href);
            if (node.title) closing += U" \"" + utf8_to_cps(*node.title) + U"\"";
            closing += U")";
        }
        const auto source_start = opening.empty() ? cursor : find_serialized(markdown, opening, cursor, limit);
        const auto content_start = (std::min)(source_start + opening.size(), limit);
        const auto content_end = map_inlines(node.children, markdown, content_start, limit, source_map);
        const auto closing_start = closing.empty() ? content_end : find_serialized(markdown, closing, content_end, limit);
        const auto source_end = (std::min)(closing_start + closing.size(), limit);
        NodeSourceRange range(node.id, char_range(source_start, source_end), char_range(content_start, closing_start));
        if (!opening.empty()) range.marker_ranges.push_back(char_range(source_start, content_start));
        if (!closing.empty()) range.marker_ranges.push_back(char_range(closing_start, source_end));
        source_map.node_ranges.push_back(std::move(range));
        return source_end;
    }

    const auto serialized = serialize_inline(node);
    const auto source_start = find_serialized(markdown, serialized, cursor, limit);
    const auto source_end = (std::min)(source_start + serialized.size(), limit);
    auto content_start = source_start;
    auto content_end = source_end;
    std::vector<CharRange> markers;
    if (node.kind == InlineKind::InlineCode || node.kind == InlineKind::InlineMath) {
        std::u32string opening;
        std::u32string closing;
        if (node.kind == InlineKind::InlineCode) {
            opening = marker_or(node.opening_marker, U"`");
            closing = marker_or(node.closing_marker, U"`");
        } else {
            const auto fallback_opening = node.math_delim == MathDelimiter::InlineParen ? std::u32string(U"\\(") : std::u32string(U"$");
            const auto fallback_closing = node.math_delim == MathDelimiter::InlineParen ? std::u32string(U"\\)") : std::u32string(U"$");
            opening = marker_or(node.opening_marker, fallback_opening);
            closing = marker_or(node.closing_marker, fallback_closing);
        }
        content_start = (std::min)(source_start + opening.size(), source_end);
        content_end = source_end >= closing.size() ? source_end - closing.size() : content_start;
        markers.push_back(char_range(source_start, content_start));
        markers.push_back(char_range(content_end, source_end));
    }
    NodeSourceRange range(node.id, char_range(source_start, source_end), char_range(content_start, content_end));
    range.marker_ranges = std::move(markers);
    source_map.node_ranges.push_back(std::move(range));
    return source_end;
}

inline std::size_t map_inlines(
    const InlineVec& nodes,
    const std::u32string& markdown,
    std::size_t cursor,
    std::size_t limit,
    SourceMap& source_map) {
    for (const auto& node : nodes) cursor = map_inline(node, markdown, cursor, limit, source_map);
    return cursor;
}

inline std::u32string block_anchor(const BlockNode& block) {
    if ((block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading) && !block.children.empty()) {
        for (const auto& child : block.children) {
            const auto value = serialize_inline(child);
            if (!value.empty()) return value;
        }
    }
    if (block.kind == BlockKind::CodeBlock && !block.code_text.empty()) return block.code_text;
    if (block.kind == BlockKind::MathBlock && !block.tex.empty()) return block.tex;
    return serialize_block(block);
}

inline std::size_t map_block(
    const BlockNode& block,
    const std::u32string& markdown,
    std::size_t source_start,
    std::size_t source_end,
    SourceMap& source_map);

inline std::size_t map_blocks_in_bounds(
    const BlockVec& blocks,
    const std::u32string& markdown,
    std::size_t cursor,
    std::size_t limit,
    SourceMap& source_map) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const auto anchor = block_anchor(blocks[index]);
        auto start = find_serialized(markdown, anchor, cursor, limit);
        if (blocks[index].kind == BlockKind::Heading) {
            const auto prefix = static_cast<std::size_t>(blocks[index].level == 0 ? 1 : blocks[index].level) + 1;
            if (start >= prefix) start -= prefix;
        }
        auto end = limit;
        if (index + 1 < blocks.size()) {
            const auto next_anchor = block_anchor(blocks[index + 1]);
            end = find_serialized(markdown, next_anchor, start, limit);
            if (blocks[index + 1].kind == BlockKind::Heading) {
                const auto prefix = static_cast<std::size_t>(blocks[index + 1].level == 0 ? 1 : blocks[index + 1].level) + 1;
                if (end >= prefix) end -= prefix;
            }
            while (end > start && (markdown[end - 1] == U' ' || markdown[end - 1] == U'\n' || markdown[end - 1] == U'>' )) --end;
        }
        cursor = map_block(blocks[index], markdown, start, end, source_map);
    }
    return cursor;
}

inline std::size_t map_list(
    const BlockNode& block,
    const std::u32string& markdown,
    std::size_t source_start,
    std::size_t source_end,
    SourceMap& source_map) {
    auto cursor = source_start;
    const auto count = block.kind == BlockKind::TaskList ? block.task_items.size() : block.list_items.size();
    for (std::size_t index = 0; index < count; ++index) {
        std::u32string marker;
        NodeId item_id;
        const BlockVec* children = nullptr;
        if (block.kind == BlockKind::TaskList) {
            const auto& item = block.task_items[index];
            marker = item.marker.empty() ? (item.checked ? U"- [x] " : U"- [ ] ") : item.marker;
            item_id = item.id;
            children = &item.children;
        } else {
            const auto& item = block.list_items[index];
            if (!item.marker.empty()) marker = item.marker;
            else if (block.list_ordered) marker = utf8_to_cps(std::to_string(block.list_start + index)) + std::u32string(1, block.list_delimiter) + U" ";
            else marker = U"- ";
            item_id = item.id;
            children = &item.children;
        }
        const auto item_body = serialize_list_item_blocks(*children);
        const auto item_text = marker + indent_continuation(item_body, marker.size());
        const auto item_start = find_serialized(markdown, item_text, cursor, source_end);
        const auto item_end = (std::min)(item_start + item_text.size(), source_end);
        const auto content_start = (std::min)(item_start + marker.size(), item_end);
        NodeSourceRange item_range(item_id, char_range(item_start, item_end), char_range(content_start, item_end));
        item_range.marker_ranges.push_back(char_range(item_start, content_start));
        source_map.node_ranges.push_back(std::move(item_range));
        map_blocks_in_bounds(*children, markdown, content_start, item_end, source_map);
        cursor = item_end;
    }
    return cursor;
}

inline std::size_t map_table_cells(
    const std::vector<TableCell>& cells,
    const std::u32string& markdown,
    std::size_t cursor,
    std::size_t limit,
    SourceMap& source_map) {
    for (const auto& cell : cells) {
        const auto content = serialize_inlines(cell.children);
        const auto boundary = cursor < limit && markdown[cursor] == U'|' ? cursor : (cursor > 0 ? cursor - 1 : cursor);
        const auto content_start = content.empty()
            ? (std::min)(boundary + 2, limit)
            : find_serialized(markdown, content, cursor, limit);
        const auto content_end = map_inlines(cell.children, markdown, content_start, limit, source_map);
        const auto source_start = boundary;
        auto source_end = content_end;
        while (source_end < limit && markdown[source_end] != U'|') ++source_end;
        if (source_end < limit) ++source_end;
        source_map.node_ranges.emplace_back(cell.id, char_range(source_start, source_end), char_range(content_start, content_end));
        cursor = source_end;
    }
    return cursor;
}

inline std::size_t map_block(
    const BlockNode& block,
    const std::u32string& markdown,
    std::size_t source_start,
    std::size_t source_end,
    SourceMap& source_map) {
    auto content_start = source_start;
    auto content_end = source_end;
    std::vector<CharRange> markers;
    if (block.kind == BlockKind::Heading) {
        const auto marker_end = (std::min)(source_start + static_cast<std::size_t>(block.level == 0 ? 1 : block.level) + 1, source_end);
        markers.push_back(char_range(source_start, marker_end));
        content_start = marker_end;
    } else if (block.kind == BlockKind::CodeBlock) {
        if (block.code_indented) {
            content_start = find_serialized(markdown, block.code_text, source_start, source_end);
        } else {
            const auto newline = markdown.find(U'\n', source_start);
            content_start = newline == std::u32string::npos || newline >= source_end ? source_end : newline + 1;
            content_end = source_end >= 3 ? source_end - 3 : content_start;
            markers.push_back(char_range(source_start, content_start));
            markers.push_back(char_range(content_end, source_end));
        }
    } else if (block.kind == BlockKind::MathBlock) {
        if (block.tex.empty()) {
            const auto newline = markdown.find(U'\n', source_start);
            content_start = newline == std::u32string::npos || newline >= source_end ? source_end : newline + 1;
        } else {
            content_start = find_serialized(markdown, block.tex, source_start, source_end);
        }
        content_end = (std::min)(content_start + block.tex.size(), source_end);
        markers.push_back(char_range(source_start, content_start));
        markers.push_back(char_range(content_end, source_end));
    }
    NodeSourceRange range(block.id, char_range(source_start, source_end), char_range(content_start, content_end));
    range.marker_ranges = std::move(markers);
    source_map.node_ranges.push_back(std::move(range));

    if (block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading) {
        map_inlines(block.children, markdown, content_start, content_end, source_map);
    } else if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
        map_blocks_in_bounds(block.quote_children, markdown, content_start, content_end, source_map);
    } else if (block.kind == BlockKind::List || block.kind == BlockKind::TaskList) {
        map_list(block, markdown, source_start, source_end, source_map);
    } else if (block.kind == BlockKind::Table) {
        auto cursor = map_table_cells(block.table_header, markdown, source_start, source_end, source_map);
        auto separator_end = markdown.find(U'\n', cursor);
        if (separator_end != std::u32string::npos && separator_end < source_end) {
            separator_end = markdown.find(U'\n', separator_end + 1);
            cursor = separator_end == std::u32string::npos || separator_end >= source_end ? source_end : separator_end + 1;
        }
        for (const auto& row : block.table_rows) {
            if (cursor < source_end && markdown[cursor] == U'\n') ++cursor;
            const auto row_start = cursor;
            cursor = map_table_cells(row.cells, markdown, cursor, source_end, source_map);
            source_map.node_ranges.emplace_back(row.id, char_range(row_start, cursor), char_range(row_start, cursor));
        }
    }
    return source_end;
}

inline SourceMap build_source_map(const EditorDocument& document, const std::u32string& markdown) {
    SourceMap source_map;
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        const auto serialized = serialize_block(document.blocks[index]);
        const auto start = find_serialized(markdown, serialized, cursor, markdown.size());
        const auto end = (std::min)(start + serialized.size(), markdown.size());
        map_block(document.blocks[index], markdown, start, end, source_map);
        if (index + 1 < document.blocks.size()) {
            for (auto& range : source_map.node_ranges) {
                if (range.node_id == document.blocks[index].id) {
                    range.source_range.end = CharOffset((std::min)(end + 1, markdown.size()));
                    break;
                }
            }
        }
        cursor = end;
        if (index + 1 < document.blocks.size()) cursor = (std::min)(cursor + 2, markdown.size());
    }
    return source_map;
}

}

inline SerializedDocument serialize_document(const EditorDocument& document) {
    SerializedDocument result;
    result.markdown = serializer_detail::serialize_blocks(document.blocks);
    result.source_map = serializer_detail::build_source_map(document, result.markdown);
    return result;
}

inline std::u32string serialize_markdown_cps(const EditorDocument& document) {
    return serialize_document(document).markdown;
}

inline std::string serialize_markdown(const EditorDocument& document) {
    return cps_to_utf8(serialize_markdown_cps(document));
}

}

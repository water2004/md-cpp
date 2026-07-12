export module elmd.core.serializer;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.source_map;
import elmd.core.utf;
import elmd.core.instrumentation;

export namespace elmd {

struct SerializedDocument {
    std::u32string markdown;
    SourceMap source_map;
};

namespace serializer_detail {

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

inline std::u32string serialize_indented_code(std::u32string_view value) {
    std::u32string result = U"    ";
    for (std::size_t index = 0; index < value.size(); ++index) {
        result.push_back(value[index]);
        if (value[index] == U'\n' && index + 1 < value.size()) result += U"    ";
    }
    return result;
}

inline std::u32string serialize_list(const BlockNode& block) {
    std::u32string result;
    const auto count = block.children.size();
    for (std::size_t index = 0; index < count; ++index) {
        const auto& item = block.children[index];
        auto marker = item.marker;
        if (marker.empty() && item.kind == BlockKind::TaskListItem) marker = item.checked ? U"- [x] " : U"- [ ] ";
        else if (marker.empty() && block.list_ordered) marker = utf8_to_cps(std::to_string(block.list_start + index)) + std::u32string(1, block.list_delimiter) + U" ";
        else if (marker.empty()) marker = U"- ";
        auto body = serialize_list_item_blocks(item.children);
        result += marker + indent_continuation(body, marker.size());
        if (index + 1 < count) result += U"\n";
    }
    return result;
}

inline std::u32string serialize_table_row(const BlockNode& row) {
    std::u32string result = U"|";
    for (const auto& cell : row.children) result += U" " + cell.inline_content.source + U" |";
    return result;
}

inline std::u32string serialize_block(const BlockNode& block) {
    switch (block.kind) {
        case BlockKind::Paragraph:
            return block.inline_content.source;
        case BlockKind::Heading:
            return std::u32string(block.level == 0 ? 1 : block.level, U'#') + U" " + block.inline_content.source;
        case BlockKind::BlockQuote: {
            auto body = serialize_blocks(block.children);
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
            if (block.code_indented) return serialize_indented_code(block.code_text);
            return U"```" + (block.language ? utf8_to_cps(*block.language) : std::u32string{}) + U"\n" + block.code_text
                + (!block.code_text.empty() && block.code_text.back() != U'\n' ? U"\n" : U"") + U"```";
        case BlockKind::MathBlock: {
            const auto newline = block.tex.empty() || block.tex.back() != U'\n' ? U"\n" : U"";
            if (block.math_delim == MathDelimiter::BlockBracket) return U"\\[\n" + block.tex + newline + U"\\]";
            if (block.math_delim == MathDelimiter::FencedMath) return U"```math\n" + block.tex + newline + U"```";
            return U"$$\n" + block.tex + newline + U"$$";
        }
        case BlockKind::Table: {
            if (block.children.empty()) return {};
            std::u32string result = serialize_table_row(block.children.front()) + U"\n|";
            for (std::size_t index = 0; index < block.children.front().children.size(); ++index) {
                auto alignment = index < block.table_aligns.size() ? block.table_aligns[index] : TableAlignment::None;
                if (alignment == TableAlignment::Left) result += U" :--- |";
                else if (alignment == TableAlignment::Center) result += U" :---: |";
                else if (alignment == TableAlignment::Right) result += U" ---: |";
                else result += U" --- |";
            }
            for (std::size_t index = 1; index < block.children.size(); ++index) result += U"\n" + serialize_table_row(block.children[index]);
            return result;
        }
        case BlockKind::ImageBlock: {
            auto result = U"![" + utf8_to_cps(block.image_alt) + U"](" + utf8_to_cps(block.src);
            if (block.image_title) result += U" \"" + utf8_to_cps(*block.image_title) + U"\"";
            return result + U")";
        }
        case BlockKind::Callout: {
            std::u32string first = U"> [!" + utf8_to_cps(block.callout_kind) + U"]";
            if (block.callout_title) first += U" " + block.callout_title->source;
            auto body = serialize_blocks(block.children);
            if (body.empty()) return first;
            std::u32string result = first;
            for (const auto& line : lines(body)) result += U"\n" + (line.empty() ? std::u32string(U">") : U"> " + line);
            return result;
        }
        case BlockKind::FootnoteDefinition: {
            auto body = serialize_blocks(block.children);
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
        case BlockKind::Document:
            return serialize_blocks(block.children);
        case BlockKind::ListItem:
        case BlockKind::TaskListItem:
            return serialize_list_item_blocks(block.children);
        case BlockKind::TableRow:
            return serialize_table_row(block);
        case BlockKind::TableCell:
            return block.inline_content.source;
    }
    return {};
}

inline bool is_empty_paragraph(const BlockNode& block) {
    return block.kind == BlockKind::Paragraph && block.inline_content.source.empty();
}

inline std::u32string_view block_separator(const BlockNode& previous, const BlockNode&) {
    if (is_empty_paragraph(previous)) return U"\n";
    const auto serialized = serialize_block(previous);
    if (!serialized.empty() && serialized.back() == U'\n') return U"\n";
    return U"\n\n";
}

inline std::u32string serialize_blocks(const BlockVec& blocks) {
    std::u32string result;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (index > 0) result += block_separator(blocks[index - 1], blocks[index]);
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

inline std::u32string block_anchor(const BlockNode& block) {
    if ((block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading)
        && !block.inline_content.source.empty()) return block.inline_content.source;
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
    const auto count = block.children.size();
    for (std::size_t index = 0; index < count; ++index) {
        const auto& item = block.children[index];
        auto marker = item.marker;
        if (marker.empty() && item.kind == BlockKind::TaskListItem) marker = item.checked ? U"- [x] " : U"- [ ] ";
        else if (marker.empty() && block.list_ordered) marker = utf8_to_cps(std::to_string(block.list_start + index)) + std::u32string(1, block.list_delimiter) + U" ";
        else if (marker.empty()) marker = U"- ";
        const auto item_body = serialize_list_item_blocks(item.children);
        const auto item_text = marker + indent_continuation(item_body, marker.size());
        const auto item_start = find_serialized(markdown, item_text, cursor, source_end);
        const auto item_end = (std::min)(item_start + item_text.size(), source_end);
        const auto content_start = (std::min)(item_start + marker.size(), item_end);
        NodeSourceRange item_range(item.id, char_range(item_start, item_end), char_range(content_start, item_end));
        item_range.marker_ranges.push_back(char_range(item_start, content_start));
        source_map.node_ranges.push_back(std::move(item_range));
        map_blocks_in_bounds(item.children, markdown, content_start, item_end, source_map);
        cursor = item_end;
    }
    return cursor;
}

inline std::size_t map_table_cells(
    const BlockNode& row,
    const std::u32string& markdown,
    std::size_t cursor,
    std::size_t limit,
    SourceMap& source_map) {
    for (const auto& cell : row.children) {
        const auto& content = cell.inline_content.source;
        const auto boundary = cursor < limit && markdown[cursor] == U'|' ? cursor : (cursor > 0 ? cursor - 1 : cursor);
        const auto content_start = content.empty()
            ? (std::min)(boundary + 2, limit)
            : find_serialized(markdown, content, cursor, limit);
        const auto content_end = (std::min)(content_start + content.size(), limit);
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
            auto line_start = source_start;
            while (line_start < source_end) {
                const auto marker_end = (std::min)(line_start + 4, source_end);
                markers.push_back(char_range(line_start, marker_end));
                auto newline = markdown.find(U'\n', marker_end);
                if (newline == std::u32string::npos || newline >= source_end) break;
                line_start = newline + 1;
            }
            content_start = markers.empty() ? source_start : markers.front().end.v;
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

    if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
        map_blocks_in_bounds(block.children, markdown, content_start, content_end, source_map);
    } else if (block.kind == BlockKind::List || block.kind == BlockKind::TaskList) {
        map_list(block, markdown, source_start, source_end, source_map);
    } else if (block.kind == BlockKind::Table) {
        if (block.children.empty()) return source_end;
        auto cursor = map_table_cells(block.children.front(), markdown, source_start, source_end, source_map);
        auto separator_end = markdown.find(U'\n', cursor);
        if (separator_end != std::u32string::npos && separator_end < source_end) {
            separator_end = markdown.find(U'\n', separator_end + 1);
            cursor = separator_end == std::u32string::npos || separator_end >= source_end ? source_end : separator_end + 1;
        }
        for (std::size_t index = 1; index < block.children.size(); ++index) {
            const auto& row = block.children[index];
            if (cursor < source_end && markdown[cursor] == U'\n') ++cursor;
            const auto row_start = cursor;
            cursor = map_table_cells(row, markdown, cursor, source_end, source_map);
            source_map.node_ranges.emplace_back(row.id, char_range(row_start, cursor), char_range(row_start, cursor));
        }
    }
    return source_end;
}

inline SourceMap build_source_map(const EditorDocument& document, const std::u32string& markdown) {
    SourceMap source_map;
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < document.root.children.size(); ++index) {
        if (index > 0) cursor += block_separator(document.root.children[index - 1], document.root.children[index]).size();
        const auto boundary = cursor;
        const auto serialized = serialize_block(document.root.children[index]);
        const auto start = (std::min)(
            index + 1 == document.root.children.size() && index > 0
                && is_empty_paragraph(document.root.children[index]) && cursor > 0 ? cursor - 1 : cursor,
            markdown.size());
        const auto end = (std::min)(start + serialized.size(), markdown.size());
        map_block(document.root.children[index], markdown, start, end, source_map);
        const auto physical_end = end < markdown.size() && markdown[end] == U'\n' ? end + 1 : end;
        for (auto& range : source_map.node_ranges) {
            if (range.node_id == document.root.children[index].id) {
                range.source_range.end = CharOffset(physical_end);
                break;
            }
        }
        cursor = (std::max)(boundary, end);
    }
    return source_map;
}

}

inline SerializedDocument serialize_document(const EditorDocument& document) {
    record_full_document_serialization();
    SerializedDocument result;
    result.markdown = serializer_detail::serialize_blocks(document.root.children);
    if (document.trailing_newline && (result.markdown.empty() || result.markdown.back() != U'\n')) result.markdown.push_back(U'\n');
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

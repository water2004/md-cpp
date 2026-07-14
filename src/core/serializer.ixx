export module elmd.core.serializer;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.utf;
import elmd.core.instrumentation;

export namespace elmd {

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

inline std::u32string serialize_footnote_definition(const BlockNode& block) {
    const auto body = serialize_blocks(block.children);
    const auto marker = block.opening_marker.empty()
        ? U"[^" + utf8_to_cps(block.footnote_label) + U"]: "
        : block.opening_marker;
    if (body.empty()) return marker;

    const auto body_lines = lines(body);
    std::u32string result = marker + body_lines.front();
    for (std::size_t index = 1; index < body_lines.size(); ++index) {
        result.push_back(U'\n');
        if (!body_lines[index].empty()) result += U"    ";
        result += body_lines[index];
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
            if (!block.opening_marker.empty() || !block.closing_marker.empty()) {
                return block.opening_marker + block.inline_content.source + block.closing_marker;
            }
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
        case BlockKind::MathBlock:
            return block.block_source.source;
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
            std::u32string first = block.opening_marker.empty()
                ? U"> [!" + utf8_to_cps(block.callout_kind) + U"]"
                : block.opening_marker;
            if (block.callout_title) {
                if (block.opening_marker.empty()) first += U" ";
                first += block.callout_title->source;
            }
            auto body = serialize_blocks(block.children);
            if (body.empty()) return first;
            std::u32string result = first;
            for (const auto& line : lines(body)) result += U"\n" + (line.empty() ? std::u32string(U">") : U"> " + line);
            return result;
        }
        case BlockKind::FootnoteDefinition: {
            return serialize_footnote_definition(block);
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

inline std::u32string block_separator(const BlockNode& previous, const BlockNode& current) {
    if (current.separator_before) return *current.separator_before;
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


}

// Serialize an already-structured Markdown fragment without treating it as a
// complete-document save. Clipboard copy and local block reparse use this
// path so normal editing never increments the full-document serialization
// counter or invents a trailing newline owned by the document.
inline std::u32string serialize_markdown_fragment(const BlockVec& blocks) {
    return serializer_detail::serialize_blocks(blocks);
}

inline std::u32string serialize_markdown_cps(const EditorDocument& document) {
    record_full_document_serialization();
    auto result = serializer_detail::serialize_blocks(document.root.children);
    if (document.trailing_newline && (result.empty() || result.back() != U'\n')) result.push_back(U'\n');
    return result;
}

inline std::string serialize_markdown(const EditorDocument& document) {
    return cps_to_utf8(serialize_markdown_cps(document));
}

}

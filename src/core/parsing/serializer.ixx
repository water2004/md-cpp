export module elmd.core.serializer;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.ids;
import elmd.core.instrumentation;
import elmd.core.text_edit;
import elmd.core.utf;

export namespace elmd {

struct SerializedSourceMap {
    NodeId container_id{};
    // Index i stores the serialized Markdown offset for source offset i.
    std::vector<std::size_t> source_to_serialized;
};

struct SerializedMarkdownProjection {
    std::u32string text;
    std::vector<SerializedSourceMap> source_maps;
};

inline std::optional<std::size_t> serialized_offset_for_source_position(
    const SerializedMarkdownProjection& projection,
    TextPosition position) {
    for (const auto& map : projection.source_maps) {
        if (map.container_id != position.container_id || map.source_to_serialized.empty()) continue;
        const auto local = (std::min)(position.source_offset, map.source_to_serialized.size() - 1);
        return map.source_to_serialized[local];
    }
    return std::nullopt;
}

inline std::optional<TextPosition> source_position_for_serialized_offset(
    const SerializedMarkdownProjection& projection,
    std::size_t serialized_offset,
    TextAffinity affinity = TextAffinity::Downstream) {
    serialized_offset = (std::min)(serialized_offset, projection.text.size());
    std::optional<TextPosition> preceding;
    std::optional<TextPosition> following;
    std::size_t preceding_offset = 0;
    std::size_t following_offset = (std::numeric_limits<std::size_t>::max)();
    for (const auto& map : projection.source_maps) {
        for (std::size_t local = 0; local < map.source_to_serialized.size(); ++local) {
            const auto projected = map.source_to_serialized[local];
            const TextPosition candidate{map.container_id, local, affinity};
            if (projected == serialized_offset) return candidate;
            if (projected < serialized_offset && (!preceding || projected >= preceding_offset)) {
                preceding = candidate;
                preceding_offset = projected;
            }
            if (projected > serialized_offset && (!following || projected < following_offset)) {
                following = candidate;
                following_offset = projected;
            }
        }
    }
    return following ? following : preceding;
}

namespace serializer_detail {

struct ProjectedText {
    std::u32string text;
    std::vector<SerializedSourceMap> source_maps;

    void append(ProjectedText other) {
        const auto shift = text.size();
        text += other.text;
        for (auto& map : other.source_maps) {
            for (auto& offset : map.source_to_serialized) offset += shift;
            source_maps.push_back(std::move(map));
        }
    }

    void append_generated(std::u32string_view value) { text.append(value); }
};

inline ProjectedText generated(std::u32string value) {
    ProjectedText result;
    result.text = std::move(value);
    return result;
}

inline ProjectedText source_text(NodeId owner, std::u32string value) {
    ProjectedText result;
    result.text = std::move(value);
    SerializedSourceMap map;
    map.container_id = owner;
    map.source_to_serialized.resize(result.text.size() + 1);
    std::iota(map.source_to_serialized.begin(), map.source_to_serialized.end(), std::size_t{0});
    result.source_maps.push_back(std::move(map));
    return result;
}

inline ProjectedText atomic_text(NodeId owner, std::u32string value) {
    ProjectedText result;
    result.text = std::move(value);
    result.source_maps.push_back({owner, {0, result.text.size()}});
    return result;
}

inline std::vector<std::size_t> line_starts(std::u32string_view value) {
    std::vector<std::size_t> result{0};
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == U'\n') result.push_back(index + 1);
    }
    return result;
}

template <class Prefix>
inline ProjectedText prefix_lines(ProjectedText input, Prefix&& prefix) {
    const auto starts = line_starts(input.text);
    std::vector<std::u32string> prefixes;
    prefixes.reserve(starts.size());
    std::vector<std::size_t> cumulative_prefix_sizes(starts.size() + 1, 0);
    std::size_t inserted = 0;
    for (std::size_t line = 0; line < starts.size(); ++line) {
        const auto end = line + 1 < starts.size() ? starts[line + 1] - 1 : input.text.size();
        prefixes.push_back(prefix(line, input.text.substr(starts[line], end - starts[line])));
        inserted += prefixes.back().size();
        cumulative_prefix_sizes[line + 1] = inserted;
    }

    ProjectedText result;
    result.text.reserve(input.text.size() + inserted);
    std::size_t start_index = 0;
    for (std::size_t offset = 0; offset <= input.text.size(); ++offset) {
        while (start_index < starts.size() && starts[start_index] == offset) {
            result.text += prefixes[start_index++];
        }
        if (offset < input.text.size()) result.text.push_back(input.text[offset]);
    }

    for (auto& map : input.source_maps) {
        for (auto& offset : map.source_to_serialized) {
            const auto prefixed_line_count = static_cast<std::size_t>(
                std::ranges::upper_bound(starts, offset) - starts.begin());
            offset += cumulative_prefix_sizes[prefixed_line_count];
        }
        result.source_maps.push_back(std::move(map));
    }
    return result;
}

inline ProjectedText serialize_block(const BlockNode& block);
inline ProjectedText serialize_blocks(const BlockVec& blocks);
inline ProjectedText serialize_blocks_from(const BlockVec& blocks, std::size_t start);

inline ProjectedText serialize_list_item_blocks(const BlockVec& blocks) {
    ProjectedText result;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (index > 0) {
            const auto nested = blocks[index].kind == BlockKind::List
                || blocks[index].kind == BlockKind::TaskList;
            result.append_generated(nested ? U"\n" : U"\n\n");
        }
        result.append(serialize_block(blocks[index]));
    }
    return result;
}

inline ProjectedText indent_continuation(ProjectedText value, std::size_t width) {
    return prefix_lines(std::move(value), [width](std::size_t line, std::u32string_view) {
        return line == 0 ? std::u32string{} : std::u32string(width, U' ');
    });
}

inline ProjectedText serialize_footnote_definition(const BlockNode& block) {
    auto const& special = block.special();
    auto body = serialize_blocks(block.children);
    const auto marker = special.opening_marker.empty()
        ? U"[^" + utf8_to_cps(special.footnote_label) + U"]: "
        : special.opening_marker;
    if (body.text.empty()) return generated(marker);
    body = prefix_lines(std::move(body), [](std::size_t line, std::u32string_view text) {
        return line > 0 && !text.empty() ? std::u32string(4, U' ') : std::u32string{};
    });
    auto result = generated(marker);
    result.append(std::move(body));
    return result;
}

inline ProjectedText serialize_list(const BlockNode& block) {
    auto const& list_special = block.special();
    ProjectedText result;
    for (std::size_t index = 0; index < block.children.size(); ++index) {
        const auto& item = block.children[index];
        auto const& item_special = item.special();
        auto marker = item_special.marker;
        if (marker.empty() && item.kind == BlockKind::TaskListItem) {
            marker = item_special.checked ? U"- [x] " : U"- [ ] ";
        } else if (marker.empty() && list_special.list_ordered) {
            marker = utf8_to_cps(std::to_string(list_special.list_start + index))
                + std::u32string(1, list_special.list_delimiter) + U" ";
        } else if (marker.empty()) {
            marker = U"- ";
        }
        result.append_generated(marker);
        result.append(indent_continuation(serialize_list_item_blocks(item.children), marker.size()));
        if (index + 1 < block.children.size()) result.append_generated(U"\n");
    }
    return result;
}

inline ProjectedText serialize_table_row(const BlockNode& row) {
    auto result = generated(U"|");
    for (const auto& cell : row.children) {
        result.append_generated(U" ");
        result.append(source_text(cell.id, cell.inline_content.source));
        result.append_generated(U" |");
    }
    return result;
}

inline ProjectedText serialize_block(const BlockNode& block) {
    auto const& special = block.special();
    switch (block.kind) {
        case BlockKind::Paragraph:
            return source_text(block.id, block.inline_content.source);
        case BlockKind::Heading: {
            auto result = generated(!special.opening_marker.empty() || !special.closing_marker.empty()
                ? special.opening_marker
                : std::u32string(special.level == 0 ? 1 : special.level, U'#') + U" ");
            result.append(source_text(block.id, block.inline_content.source));
            result.append_generated(special.closing_marker);
            return result;
        }
        case BlockKind::BlockQuote:
            return prefix_lines(serialize_blocks(block.children), [](std::size_t, std::u32string_view line) {
                return line.empty() ? std::u32string(U">") : std::u32string(U"> ");
            });
        case BlockKind::List:
        case BlockKind::TaskList:
            return serialize_list(block);
        case BlockKind::CodeBlock:
        case BlockKind::MathBlock:
            return source_text(block.id, block.block_source.source());
        case BlockKind::Table: {
            if (block.children.empty()) return {};
            auto result = serialize_table_row(block.children.front());
            result.append_generated(U"\n|");
            for (std::size_t index = 0; index < block.children.front().children.size(); ++index) {
                const auto alignment = index < special.table_aligns.size()
                    ? special.table_aligns[index] : TableAlignment::None;
                if (alignment == TableAlignment::Left) result.append_generated(U" :--- |");
                else if (alignment == TableAlignment::Center) result.append_generated(U" :---: |");
                else if (alignment == TableAlignment::Right) result.append_generated(U" ---: |");
                else result.append_generated(U" --- |");
            }
            for (std::size_t index = 1; index < block.children.size(); ++index) {
                result.append_generated(U"\n");
                result.append(serialize_table_row(block.children[index]));
            }
            return result;
        }
        case BlockKind::ImageBlock: {
            auto value = U"![" + utf8_to_cps(special.image_alt) + U"](" + utf8_to_cps(special.src);
            if (special.image_title) value += U" \"" + utf8_to_cps(*special.image_title) + U"\"";
            return atomic_text(block.id, value + U")");
        }
        case BlockKind::Callout: {
            auto first = special.opening_marker.empty()
                ? U"> [!" + utf8_to_cps(special.callout_kind) + U"]"
                : special.opening_marker;
            auto result = generated(first);
            const auto* title = callout_title_block(block);
            if (title) {
                if (special.opening_marker.empty()) result.append_generated(U" ");
                result.append(source_text(title->id, title->inline_content.source));
            }
            auto body = serialize_blocks_from(block.children, callout_body_start(block));
            if (!body.text.empty()) {
                result.append_generated(U"\n");
                result.append(prefix_lines(std::move(body), [](std::size_t, std::u32string_view line) {
                    return line.empty() ? std::u32string(U">") : std::u32string(U"> ");
                }));
            }
            return result;
        }
        case BlockKind::FootnoteDefinition:
            return serialize_footnote_definition(block);
        case BlockKind::Toc:
            return atomic_text(block.id, special.toc_marker == TocMarkerKind::WikiToc ? U"[[TOC]]" : U"[TOC]");
        case BlockKind::Frontmatter: {
            const std::u32string marker = special.fmt == FrontmatterFormat::Toml ? U"+++" : U"---";
            return atomic_text(block.id, marker + U"\n" + utf8_to_cps(special.raw) + U"\n" + marker);
        }
        case BlockKind::ThematicBreak:
            return atomic_text(block.id, U"---");
        case BlockKind::LinkDefinition:
        case BlockKind::UnsupportedMarkup:
            return atomic_text(block.id, utf8_to_cps(special.raw));
        case BlockKind::Extension:
            return atomic_text(block.id, U"[ext:" + utf8_to_cps(special.ext_name) + U"]");
        case BlockKind::Document:
            return serialize_blocks(block.children);
        case BlockKind::ListItem:
        case BlockKind::TaskListItem:
            return serialize_list_item_blocks(block.children);
        case BlockKind::CalloutTitle:
            return source_text(block.id, block.inline_content.source);
        case BlockKind::TableRow:
            return serialize_table_row(block);
        case BlockKind::TableCell:
            return source_text(block.id, block.inline_content.source);
    }
    return {};
}

inline bool is_empty_paragraph(const BlockNode& block) {
    return block.kind == BlockKind::Paragraph && block.inline_content.source.empty();
}

inline std::u32string block_separator(const BlockNode& previous, const BlockNode& current) {
    if (current.special().separator_before) return *current.special().separator_before;
    if (is_empty_paragraph(previous)) return U"\n";
    const auto serialized = serialize_block(previous);
    if (!serialized.text.empty() && serialized.text.back() == U'\n') return U"\n";
    return U"\n\n";
}

inline ProjectedText serialize_blocks(const BlockVec& blocks) {
    return serialize_blocks_from(blocks, 0);
}

inline ProjectedText serialize_blocks_from(const BlockVec& blocks, std::size_t start) {
    ProjectedText result;
    start = (std::min)(start, blocks.size());
    for (std::size_t index = start; index < blocks.size(); ++index) {
        if (index > start) result.append_generated(block_separator(blocks[index - 1], blocks[index]));
        result.append(serialize_block(blocks[index]));
    }
    return result;
}

inline SerializedMarkdownProjection finish(ProjectedText projected) {
    return {std::move(projected.text), std::move(projected.source_maps)};
}

}  // namespace serializer_detail

// Serialize an already-structured Markdown fragment without treating it as a
// complete-document save. Clipboard copy and local block reparse use this
// path so normal editing never increments the full-document serialization
// counter or invents a trailing newline owned by the document.
inline std::u32string serialize_markdown_fragment(const BlockVec& blocks) {
    return serializer_detail::serialize_blocks(blocks).text;
}

inline SerializedMarkdownProjection serialize_markdown_projection(const EditorDocument& document) {
    record_full_document_serialization();
    auto projected = serializer_detail::serialize_blocks(document.root.children);
    if (document.trailing_newline && (projected.text.empty() || projected.text.back() != U'\n')) {
        projected.append_generated(U"\n");
    }
    return serializer_detail::finish(std::move(projected));
}

inline std::u32string serialize_markdown_cps(const EditorDocument& document) {
    return serialize_markdown_projection(document).text;
}

inline std::string serialize_markdown(const EditorDocument& document) {
    return cps_to_utf8(serialize_markdown_cps(document));
}

}  // namespace elmd

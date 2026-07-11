export module elmd.core.document_projection;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.document_position;
import elmd.core.diagnostics;
import elmd.core.metadata;
import elmd.core.outline;
import elmd.core.parser;
import elmd.core.serializer;
import elmd.core.source_map;
import elmd.core.symbols;
import elmd.core.utf;

export namespace elmd {

struct DocumentProjection {
    std::u32string markdown;
    SourceMap source_map;
    DocumentMetadata metadata;
    std::vector<Diagnostic> diagnostics;
    DocumentSymbolIndex symbols;
    Outline outline;
};

namespace document_projection_detail {

using NodeIdMap = std::unordered_map<std::uint64_t, NodeId>;

inline void bind_inline_ids(const InlineVec& authoritative, const InlineVec& parsed, NodeIdMap& ids) {
    const auto count = (std::min)(authoritative.size(), parsed.size());
    for (std::size_t index = 0; index < count; ++index) {
        ids[parsed[index].id.v] = authoritative[index].id;
        bind_inline_ids(authoritative[index].children, parsed[index].children, ids);
    }
}

inline void bind_block_ids(const BlockVec& authoritative, const BlockVec& parsed, NodeIdMap& ids) {
    std::size_t parsed_index = 0;
    for (const auto& block : authoritative) {
        while (parsed_index < parsed.size() && parsed[parsed_index].kind != block.kind) ++parsed_index;
        if (parsed_index >= parsed.size()) break;
        const auto& parsed_block = parsed[parsed_index++];
        ids[parsed_block.id.v] = block.id;
        bind_inline_ids(block.children, parsed_block.children, ids);
        bind_block_ids(block.quote_children, parsed_block.quote_children, ids);

        const auto list_count = (std::min)(block.list_items.size(), parsed_block.list_items.size());
        for (std::size_t item_index = 0; item_index < list_count; ++item_index) {
            ids[parsed_block.list_items[item_index].id.v] = block.list_items[item_index].id;
            bind_block_ids(block.list_items[item_index].children, parsed_block.list_items[item_index].children, ids);
        }
        const auto task_count = (std::min)(block.task_items.size(), parsed_block.task_items.size());
        for (std::size_t item_index = 0; item_index < task_count; ++item_index) {
            ids[parsed_block.task_items[item_index].id.v] = block.task_items[item_index].id;
            bind_block_ids(block.task_items[item_index].children, parsed_block.task_items[item_index].children, ids);
        }

        const auto header_count = (std::min)(block.table_header.size(), parsed_block.table_header.size());
        for (std::size_t cell_index = 0; cell_index < header_count; ++cell_index) {
            ids[parsed_block.table_header[cell_index].id.v] = block.table_header[cell_index].id;
            bind_inline_ids(block.table_header[cell_index].children, parsed_block.table_header[cell_index].children, ids);
        }
        const auto row_count = (std::min)(block.table_rows.size(), parsed_block.table_rows.size());
        for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
            ids[parsed_block.table_rows[row_index].id.v] = block.table_rows[row_index].id;
            const auto cell_count = (std::min)(block.table_rows[row_index].cells.size(), parsed_block.table_rows[row_index].cells.size());
            for (std::size_t cell_index = 0; cell_index < cell_count; ++cell_index) {
                ids[parsed_block.table_rows[row_index].cells[cell_index].id.v] = block.table_rows[row_index].cells[cell_index].id;
                bind_inline_ids(block.table_rows[row_index].cells[cell_index].children, parsed_block.table_rows[row_index].cells[cell_index].children, ids);
            }
        }
    }
}

inline NodeId rebound(NodeId id, const NodeIdMap& ids) {
    if (const auto found = ids.find(id.v); found != ids.end()) return found->second;
    return {};
}

inline void rebind_symbols(DocumentSymbolIndex& symbols, const NodeIdMap& ids) {
    for (auto& symbol : symbols.headings) symbol.node_id = rebound(symbol.node_id, ids);
    for (auto& symbol : symbols.footnotes) symbol.node_id = rebound(symbol.node_id, ids);
    for (auto& symbol : symbols.links) symbol.node_id = rebound(symbol.node_id, ids);
    for (auto& symbol : symbols.images) symbol.node_id = rebound(symbol.node_id, ids);
    for (auto& symbol : symbols.math_blocks) symbol.node_id = rebound(symbol.node_id, ids);
    for (auto& symbol : symbols.code_blocks) symbol.node_id = rebound(symbol.node_id, ids);
}

inline void add_missing_top_level_ranges(const EditorDocument& document, SourceMap& source_map) {
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        EditorDocument fragment;
        fragment.revision = document.revision;
        fragment.blocks.push_back(document.blocks[index]);
        const auto serialized = serialize_markdown_cps(fragment);
        if (!source_map.find_node_by_id(document.blocks[index].id)) {
            NodeSourceRange range(
                document.blocks[index].id,
                CharRange(CharOffset(cursor), CharOffset(cursor + serialized.size())),
                CharRange(CharOffset(cursor), CharOffset(cursor + serialized.size())));
            source_map.node_ranges.push_back(std::move(range));
        }
        cursor += serialized.size();
        if (index + 1 < document.blocks.size()) cursor += 2;
    }
}

inline const BlockNode* find_paragraph(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) return &block;
        if (const auto* nested = find_paragraph(block.quote_children, id)) return nested;
        for (const auto& item : block.list_items) if (const auto* nested = find_paragraph(item.children, id)) return nested;
        for (const auto& item : block.task_items) if (const auto* nested = find_paragraph(item.children, id)) return nested;
    }
    return nullptr;
}

inline std::size_t logical_offset_from_source(
    const EditorDocument& document,
    const InlineVec& nodes,
    CharOffset source_offset,
    CharOffset fallback_start) {
    std::size_t consumed = 0;
    auto cursor = fallback_start.v;
    for (const auto& node : nodes) {
        const auto length = inline_text_content(node).size();
        const auto* range = document.source_map.find_node_by_id(node.id);
        const auto source_start = range ? range->source_range.start.v : cursor;
        const auto source_end = range ? range->source_range.end.v : source_start + length;
        if (source_offset.v < source_start) return consumed;
        if (source_offset.v > source_end) {
            consumed += length;
            cursor = source_end;
            continue;
        }
        const bool container = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
            || node.kind == InlineKind::Strike || node.kind == InlineKind::Span || node.kind == InlineKind::Link;
        if (container) {
            const auto content_start = range ? range->content_range.start : CharOffset(source_start);
            return consumed + logical_offset_from_source(document, node.children, source_offset, content_start);
        }
        const auto content_start = range ? range->content_range.start.v : source_start;
        if (source_offset.v <= content_start) return consumed;
        return consumed + (std::min)(source_offset.v - content_start, length);
    }
    return consumed;
}

inline std::optional<CharOffset> source_offset_from_logical(
    const EditorDocument& document,
    const InlineVec& nodes,
    std::size_t logical_offset,
    CharOffset fallback_start) {
    std::size_t consumed = 0;
    auto cursor = fallback_start.v;
    for (const auto& node : nodes) {
        const auto length = inline_text_content(node).size();
        if (logical_offset > consumed + length) {
            consumed += length;
            if (const auto* range = document.source_map.find_node_by_id(node.id)) cursor = range->source_range.end.v;
            else cursor += length;
            continue;
        }
        const auto* range = document.source_map.find_node_by_id(node.id);
        const auto local = logical_offset >= consumed ? logical_offset - consumed : 0;
        const bool container = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
            || node.kind == InlineKind::Strike || node.kind == InlineKind::Span || node.kind == InlineKind::Link;
        if (container) {
            const auto content_start = range ? range->content_range.start : CharOffset(cursor);
            if (auto nested = source_offset_from_logical(document, node.children, local, content_start)) return nested;
            return range ? range->content_range.end : CharOffset(cursor);
        }
        const auto content_start = range ? range->content_range.start.v : cursor;
        return CharOffset(content_start + (std::min)(local, length));
    }
    return std::nullopt;
}

inline std::optional<DocumentPosition> position_in_blocks(
    const EditorDocument& document,
    const BlockVec& blocks,
    CharOffset source_offset) {
    for (const auto& block : blocks) {
        if (block.kind == BlockKind::Paragraph) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (range && source_offset.v >= range->content_range.start.v && source_offset.v <= range->content_range.end.v) {
                const auto offset = logical_offset_from_source(document, block.children, source_offset, range->content_range.start);
                return DocumentPosition{block.id, offset, TextAffinity::Downstream};
            }
        }
        if (block.kind == BlockKind::CodeBlock) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (range && source_offset.v >= range->content_range.start.v && source_offset.v <= range->content_range.end.v) {
                if (!block.code_indented) {
                    return DocumentPosition{block.id, source_offset.v - range->content_range.start.v, TextAffinity::Downstream};
                }
                std::size_t logical_start = 0;
                std::size_t text_start = 0;
                for (std::size_t line = 0; line < range->marker_ranges.size(); ++line) {
                    auto text_end = block.code_text.find(U'\n', text_start);
                    if (text_end == std::u32string::npos) text_end = block.code_text.size();
                    const auto source_start = range->marker_ranges[line].end.v;
                    const auto source_end = line + 1 < range->marker_ranges.size()
                        ? range->marker_ranges[line + 1].start.v - 1
                        : range->content_range.end.v;
                    if (source_offset.v >= range->marker_ranges[line].start.v && source_offset.v <= source_end) {
                        return DocumentPosition{
                            block.id,
                            logical_start + (std::min)(source_offset.v > source_start ? source_offset.v - source_start : 0, text_end - text_start),
                            TextAffinity::Downstream};
                    }
                    logical_start += text_end - text_start;
                    if (text_end < block.code_text.size()) ++logical_start;
                    text_start = text_end < block.code_text.size() ? text_end + 1 : text_end;
                }
            }
        }
        if (auto position = position_in_blocks(document, block.quote_children, source_offset)) return position;
        for (const auto& item : block.list_items) {
            if (auto position = position_in_blocks(document, item.children, source_offset)) return position;
        }
        for (const auto& item : block.task_items) {
            if (auto position = position_in_blocks(document, item.children, source_offset)) return position;
        }
    }
    return std::nullopt;
}

}

inline DocumentProjection project_document(const EditorDocument& document, const MarkdownDialect& dialect = default_dialect()) {
    DocumentProjection projection;
    projection.markdown = serialize_markdown_cps(document);
    auto parsed = parse_text(document.revision, cps_to_utf8(projection.markdown), dialect);

    document_projection_detail::NodeIdMap ids;
    document_projection_detail::bind_block_ids(document.blocks, parsed.document.blocks, ids);
    for (auto range : parsed.document.source_map.node_ranges) {
        range.node_id = document_projection_detail::rebound(range.node_id, ids);
        if (range.node_id.v != 0) projection.source_map.node_ranges.push_back(std::move(range));
    }
    document_projection_detail::add_missing_top_level_ranges(document, projection.source_map);
    projection.metadata = std::move(parsed.document.metadata);
    projection.diagnostics = std::move(parsed.document.diagnostics);
    projection.symbols = std::move(parsed.symbols);
    document_projection_detail::rebind_symbols(projection.symbols, ids);
    projection.outline = build_outline_from_blocks(document.revision, document.blocks);
    return projection;
}

inline std::optional<DocumentPosition> document_position_from_source_offset(
    const EditorDocument& document,
    CharOffset source_offset) {
    return document_projection_detail::position_in_blocks(document, document.blocks, source_offset);
}

inline std::optional<CharOffset> source_offset_from_document_position(
    const EditorDocument& document,
    DocumentPosition position) {
    const auto* paragraph = document_projection_detail::find_paragraph(document.blocks, position.node_id);
    const auto* range = document.source_map.find_node_by_id(position.node_id);
    if (paragraph && range) {
        const auto logical_length = block_inline_text_content(paragraph->children).size();
        if (paragraph->children.empty()) return range->content_range.start;
        if (auto source = document_projection_detail::source_offset_from_logical(
                document, paragraph->children, (std::min)(position.offset, logical_length), range->content_range.start)) return source;
        return range->content_range.end;
    }
    std::function<const BlockNode*(const BlockVec&)> find_code = [&](const BlockVec& blocks) -> const BlockNode* {
        for (const auto& block : blocks) {
            if (block.id == position.node_id && block.kind == BlockKind::CodeBlock) return &block;
            if (const auto* nested = find_code(block.quote_children)) return nested;
            for (const auto& item : block.list_items) if (const auto* nested = find_code(item.children)) return nested;
            for (const auto& item : block.task_items) if (const auto* nested = find_code(item.children)) return nested;
        }
        return nullptr;
    };
    const auto* code = find_code(document.blocks);
    if (!code || !range) return std::nullopt;
    const auto offset = (std::min)(position.offset, code->code_text.size());
    if (!code->code_indented) return CharOffset(range->content_range.start.v + offset);
    std::size_t logical_start = 0;
    std::size_t text_start = 0;
    for (std::size_t line = 0; line < range->marker_ranges.size(); ++line) {
        auto text_end = code->code_text.find(U'\n', text_start);
        if (text_end == std::u32string::npos) text_end = code->code_text.size();
        const auto line_length = text_end - text_start;
        if (offset <= logical_start + line_length) {
            return CharOffset(range->marker_ranges[line].end.v + offset - logical_start);
        }
        logical_start += line_length;
        if (text_end < code->code_text.size()) ++logical_start;
        text_start = text_end < code->code_text.size() ? text_end + 1 : text_end;
    }
    return range->content_range.end;
}

}

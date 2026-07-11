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

inline std::optional<DocumentPosition> position_in_blocks(
    const EditorDocument& document,
    const BlockVec& blocks,
    CharOffset source_offset) {
    for (const auto& block : blocks) {
        if (block.kind == BlockKind::Paragraph) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (range && source_offset.v >= range->content_range.start.v && source_offset.v <= range->content_range.end.v) {
                const auto offset = (std::min)(
                    source_offset.v - range->content_range.start.v,
                    block_inline_text_content(block.children).size());
                return DocumentPosition{block.id, offset, TextAffinity::Downstream};
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
    if (!paragraph || !range) return std::nullopt;
    const auto logical_length = block_inline_text_content(paragraph->children).size();
    return CharOffset(range->content_range.start.v + (std::min)(position.offset, logical_length));
}

}

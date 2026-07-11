export module elmd.core.document_projection;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.document_position;
import elmd.core.diagnostics;
import elmd.core.metadata;
import elmd.core.outline;
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

inline void collect_inline_symbols(const InlineVec& nodes, DocumentSymbolIndex& symbols) {
    for (const auto& node : nodes) {
        if (node.kind == InlineKind::Link) {
            symbols.links.push_back(LinkSymbol{node.id, node.href, cps_to_utf8(block_inline_text_content(node.children))});
        } else if (node.kind == InlineKind::Image) {
            symbols.images.push_back(ImageSymbol{node.id, node.href, node.alt});
        }
        collect_inline_symbols(node.children, symbols);
    }
}

inline std::size_t line_count(std::u32string_view text) {
    return text.empty() ? 0 : 1 + static_cast<std::size_t>(std::count(text.begin(), text.end(), U'\n'));
}

inline void collect_block_symbols(const BlockVec& blocks, DocumentSymbolIndex& symbols) {
    for (const auto& block : blocks) {
        if (block.kind == BlockKind::Heading) {
            symbols.headings.push_back(HeadingSymbol{block.id, block.level, cps_to_utf8(block_inline_text_content(block.children)), block.slug});
        } else if (block.kind == BlockKind::FootnoteDefinition) {
            symbols.footnotes.push_back(FootnoteSymbol{block.id, block.footnote_label});
        } else if (block.kind == BlockKind::ImageBlock) {
            symbols.images.push_back(ImageSymbol{block.id, block.src, block.image_alt});
        } else if (block.kind == BlockKind::MathBlock) {
            symbols.math_blocks.push_back(MathSymbol{block.id, cps_to_utf8(block.tex)});
        } else if (block.kind == BlockKind::CodeBlock) {
            symbols.code_blocks.push_back(CodeBlockSymbol{block.id, block.language, line_count(block.code_text)});
        }
        collect_inline_symbols(block.children, symbols);
        collect_block_symbols(block.quote_children, symbols);
        for (const auto& item : block.list_items) collect_block_symbols(item.children, symbols);
        for (const auto& item : block.task_items) collect_block_symbols(item.children, symbols);
        for (const auto& cell : block.table_header) collect_inline_symbols(cell.children, symbols);
        for (const auto& row : block.table_rows) for (const auto& cell : row.cells) collect_inline_symbols(cell.children, symbols);
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
    static_cast<void>(dialect);
    DocumentProjection projection;
    auto serialized = serialize_document(document);
    projection.markdown = std::move(serialized.markdown);
    projection.source_map = std::move(serialized.source_map);
    projection.metadata = document.metadata;
    projection.diagnostics = document.diagnostics;
    document_projection_detail::collect_block_symbols(document.blocks, projection.symbols);
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

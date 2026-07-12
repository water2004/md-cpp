export module elmd.core.document_projection;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.document_position;
import elmd.core.selection;
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

inline const BlockNode* find_text_block(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if (block.id == id && (block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading)) return &block;
        if (const auto* nested = find_text_block(block.quote_children, id)) return nested;
        for (const auto& item : block.list_items) if (const auto* nested = find_text_block(item.children, id)) return nested;
        for (const auto& item : block.task_items) if (const auto* nested = find_text_block(item.children, id)) return nested;
    }
    return nullptr;
}

inline bool atomic_editable_kind(BlockKind kind) {
    return kind == BlockKind::ImageBlock || kind == BlockKind::Toc || kind == BlockKind::Frontmatter
        || kind == BlockKind::ThematicBreak || kind == BlockKind::LinkDefinition
        || kind == BlockKind::UnsupportedMarkup || kind == BlockKind::Extension;
}

inline std::optional<DocumentPosition> first_position_in_blocks(const BlockVec& blocks) {
    for (const auto& block : blocks) {
        if (block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading
            || block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
            return DocumentPosition{block.id, 0, TextAffinity::Downstream};
        }
        if (atomic_editable_kind(block.kind)) return DocumentPosition{block.id, 0, TextAffinity::Upstream};
        if (auto position = first_position_in_blocks(block.quote_children)) return position;
        for (const auto& item : block.list_items) if (auto position = first_position_in_blocks(item.children)) return position;
        for (const auto& item : block.task_items) if (auto position = first_position_in_blocks(item.children)) return position;
        if (block.kind == BlockKind::Table && !block.table_header.empty()) {
            return DocumentPosition{block.table_header.front().id, 0, TextAffinity::Downstream};
        }
    }
    return std::nullopt;
}

inline std::optional<DocumentPosition> last_position_in_blocks(const BlockVec& blocks) {
    for (auto block = blocks.rbegin(); block != blocks.rend(); ++block) {
        if (block->kind == BlockKind::Paragraph || block->kind == BlockKind::Heading
            || block->kind == BlockKind::CodeBlock || block->kind == BlockKind::MathBlock) {
            const auto length = block->kind == BlockKind::CodeBlock ? block->code_text.size()
                : block->kind == BlockKind::MathBlock ? block->tex.size()
                : block_inline_text_content(block->children).size();
            return DocumentPosition{block->id, length, TextAffinity::Downstream};
        }
        if (atomic_editable_kind(block->kind)) return DocumentPosition{block->id, 1, TextAffinity::Downstream};
        if (auto position = last_position_in_blocks(block->quote_children)) return position;
        for (auto item = block->task_items.rbegin(); item != block->task_items.rend(); ++item) {
            if (auto position = last_position_in_blocks(item->children)) return position;
        }
        for (auto item = block->list_items.rbegin(); item != block->list_items.rend(); ++item) {
            if (auto position = last_position_in_blocks(item->children)) return position;
        }
        if (block->kind == BlockKind::Table) {
            if (!block->table_rows.empty() && !block->table_rows.back().cells.empty()) {
                const auto& cell = block->table_rows.back().cells.back();
                return DocumentPosition{cell.id, block_inline_text_content(cell.children).size(), TextAffinity::Downstream};
            }
            if (!block->table_header.empty()) {
                const auto& cell = block->table_header.back();
                return DocumentPosition{cell.id, block_inline_text_content(cell.children).size(), TextAffinity::Downstream};
            }
        }
    }
    return std::nullopt;
}

inline std::optional<DocumentPosition> position_near_blocks(
    const EditorDocument& document,
    const BlockVec& blocks,
    CharOffset source_offset) {
    const BlockNode* nearest = nullptr;
    const NodeSourceRange* nearest_range = nullptr;
    std::size_t nearest_distance = (std::numeric_limits<std::size_t>::max)();
    for (const auto& block : blocks) {
        const auto* range = document.source_map.find_node_by_id(block.id);
        if (!range) continue;
        const auto distance = source_offset.v < range->source_range.start.v
            ? range->source_range.start.v - source_offset.v
            : source_offset.v > range->source_range.end.v
                ? source_offset.v - range->source_range.end.v
                : 0;
        if (distance < nearest_distance) {
            nearest = &block;
            nearest_range = range;
            nearest_distance = distance;
        }
    }
    if (!nearest || !nearest_range) return std::nullopt;
    BlockVec single{*nearest};
    return source_offset.v <= nearest_range->content_range.start.v
        ? first_position_in_blocks(single)
        : last_position_in_blocks(single);
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

struct InlineDocumentPosition {
    std::size_t logical_offset = 0;
    NodeId inline_node_id{};
    DocumentPositionPart part = DocumentPositionPart::Content;
    std::size_t part_offset = 0;
};

inline std::optional<InlineDocumentPosition> inline_position_from_source(
    const EditorDocument& document,
    const InlineVec& nodes,
    CharOffset source_offset,
    std::size_t logical_base = 0) {
    std::size_t consumed = logical_base;
    for (const auto& node : nodes) {
        const auto length = inline_text_content(node).size();
        const auto* range = document.source_map.find_node_by_id(node.id);
        if (!range || source_offset.v < range->source_range.start.v || source_offset.v > range->source_range.end.v) {
            consumed += length;
            continue;
        }
        if (source_offset.v < range->content_range.start.v) {
            return InlineDocumentPosition{
                consumed,
                node.id,
                DocumentPositionPart::OpeningMarker,
                source_offset.v - range->source_range.start.v};
        }
        if (source_offset.v > range->content_range.end.v) {
            return InlineDocumentPosition{
                consumed + length,
                node.id,
                DocumentPositionPart::ClosingMarker,
                source_offset.v - range->content_range.end.v};
        }
        const bool container = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
            || node.kind == InlineKind::Strike || node.kind == InlineKind::Span || node.kind == InlineKind::Link;
        if (container) {
            if (auto nested = inline_position_from_source(document, node.children, source_offset, consumed)) {
                if (nested->inline_node_id.v != 0 || node.kind == InlineKind::Span || node.kind == InlineKind::Link) return nested;
            }
            const auto local = (std::min)(source_offset.v - range->content_range.start.v, length);
            return InlineDocumentPosition{
                consumed + local,
                node.id,
                DocumentPositionPart::Content,
                local};
        }
        if (node.kind == InlineKind::InlineCode || node.kind == InlineKind::InlineMath) {
            return InlineDocumentPosition{
                consumed + (std::min)(source_offset.v - range->content_range.start.v, length),
                node.id,
                DocumentPositionPart::Content,
                (std::min)(source_offset.v - range->content_range.start.v, length)};
        }
        return InlineDocumentPosition{consumed + logical_offset_from_source(
            document, InlineVec{node}, source_offset, range->content_range.start)};
    }
    return std::nullopt;
}

inline std::optional<CharOffset> source_offset_from_logical(
    const EditorDocument& document,
    const InlineVec& nodes,
    std::size_t logical_offset,
    CharOffset fallback_start,
    TextAffinity affinity) {
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
            if (local == length && affinity == TextAffinity::Downstream && range) return range->source_range.end;
            const auto content_start = range ? range->content_range.start : CharOffset(cursor);
            if (auto nested = source_offset_from_logical(document, node.children, local, content_start, affinity)) return nested;
            return range ? range->content_range.end : CharOffset(cursor);
        }
        if ((node.kind == InlineKind::InlineCode || node.kind == InlineKind::InlineMath)
            && local == length && affinity == TextAffinity::Downstream && range) return range->source_range.end;
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
        if (block.kind != BlockKind::Paragraph || !block.children.empty()) continue;
        const auto* range = document.source_map.find_node_by_id(block.id);
        if (range && source_offset.v >= range->source_range.start.v
            && source_offset.v <= range->source_range.end.v) {
            return DocumentPosition{
                block.id,
                0,
                source_offset.v <= range->content_range.start.v
                    ? TextAffinity::Upstream
                    : TextAffinity::Downstream};
        }
    }
    for (const auto& block : blocks) {
        if (block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (block.kind == BlockKind::Heading && range
                && source_offset.v >= range->source_range.start.v
                && source_offset.v < range->content_range.start.v) {
                return DocumentPosition{
                    block.id,
                    0,
                    TextAffinity::Upstream,
                    NodeId{},
                    DocumentPositionPart::OpeningMarker,
                    source_offset.v - range->source_range.start.v};
            }
            if (range && source_offset.v >= range->content_range.start.v && source_offset.v <= range->content_range.end.v) {
                if (auto inline_position = inline_position_from_source(document, block.children, source_offset)) {
                    return DocumentPosition{
                        block.id,
                        inline_position->logical_offset,
                        inline_position->part == DocumentPositionPart::OpeningMarker && inline_position->part_offset == 0
                            ? TextAffinity::Upstream
                            : TextAffinity::Downstream,
                        inline_position->inline_node_id,
                        inline_position->part,
                        inline_position->part_offset};
                }
                const auto offset = logical_offset_from_source(document, block.children, source_offset, range->content_range.start);
                return DocumentPosition{block.id, offset, TextAffinity::Downstream};
            }
        }
        if (block.kind == BlockKind::CodeBlock) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (range && source_offset.v >= range->source_range.start.v && source_offset.v < range->content_range.start.v) {
                return DocumentPosition{
                    block.id, 0, TextAffinity::Upstream, NodeId{}, DocumentPositionPart::OpeningMarker,
                    source_offset.v - range->source_range.start.v};
            }
            if (range && source_offset.v > range->content_range.end.v && source_offset.v <= range->source_range.end.v) {
                return DocumentPosition{
                    block.id, block.code_text.size(), TextAffinity::Downstream, NodeId{}, DocumentPositionPart::ClosingMarker,
                    source_offset.v - range->content_range.end.v};
            }
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
        if (block.kind == BlockKind::MathBlock) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (range && source_offset.v >= range->source_range.start.v && source_offset.v < range->content_range.start.v) {
                return DocumentPosition{
                    block.id, 0, TextAffinity::Upstream, NodeId{}, DocumentPositionPart::OpeningMarker,
                    source_offset.v - range->source_range.start.v};
            }
            if (range && source_offset.v > range->content_range.end.v && source_offset.v <= range->source_range.end.v) {
                return DocumentPosition{
                    block.id, block.tex.size(), TextAffinity::Downstream, NodeId{}, DocumentPositionPart::ClosingMarker,
                    source_offset.v - range->content_range.end.v};
            }
            if (range && source_offset.v >= range->content_range.start.v && source_offset.v <= range->content_range.end.v) {
                return DocumentPosition{block.id, source_offset.v - range->content_range.start.v, TextAffinity::Downstream};
            }
        }
        if (block.kind == BlockKind::Table) {
            auto position_in_cell = [&](const TableCell& cell) -> std::optional<DocumentPosition> {
                const auto* range = document.source_map.find_node_by_id(cell.id);
                if (!range || source_offset.v < range->source_range.start.v || source_offset.v > range->source_range.end.v) return std::nullopt;
                const auto content_offset = CharOffset((std::clamp)(
                    source_offset.v, range->content_range.start.v, range->content_range.end.v));
                if (auto inline_position = inline_position_from_source(document, cell.children, content_offset)) {
                    return DocumentPosition{
                        cell.id,
                        inline_position->logical_offset,
                        inline_position->part == DocumentPositionPart::OpeningMarker && inline_position->part_offset == 0
                            ? TextAffinity::Upstream
                            : TextAffinity::Downstream,
                        inline_position->inline_node_id,
                        inline_position->part,
                        inline_position->part_offset};
                }
                const auto offset = logical_offset_from_source(document, cell.children, content_offset, range->content_range.start);
                return DocumentPosition{cell.id, offset, TextAffinity::Downstream};
            };
            for (const auto& cell : block.table_header) if (auto position = position_in_cell(cell)) return position;
            for (const auto& row : block.table_rows) {
                for (const auto& cell : row.cells) if (auto position = position_in_cell(cell)) return position;
            }
            const auto* table_range = document.source_map.find_node_by_id(block.id);
            if (table_range && source_offset.v >= table_range->source_range.start.v
                && source_offset.v <= table_range->source_range.end.v) {
                const TableCell* nearest = nullptr;
                const NodeSourceRange* nearest_range = nullptr;
                std::size_t nearest_distance = (std::numeric_limits<std::size_t>::max)();
                auto consider = [&](const TableCell& cell) {
                    const auto* range = document.source_map.find_node_by_id(cell.id);
                    if (!range) return;
                    const auto distance = source_offset.v < range->source_range.start.v
                        ? range->source_range.start.v - source_offset.v
                        : source_offset.v > range->source_range.end.v
                            ? source_offset.v - range->source_range.end.v
                            : 0;
                    if (distance < nearest_distance) {
                        nearest = &cell;
                        nearest_range = range;
                        nearest_distance = distance;
                    }
                };
                for (const auto& cell : block.table_header) consider(cell);
                for (const auto& row : block.table_rows) for (const auto& cell : row.cells) consider(cell);
                if (nearest && nearest_range) {
                    const auto after = source_offset.v > nearest_range->source_range.end.v;
                    return DocumentPosition{
                        nearest->id,
                        after ? block_inline_text_content(nearest->children).size() : 0,
                        after ? TextAffinity::Downstream : TextAffinity::Upstream};
                }
            }
        }
        if (atomic_editable_kind(block.kind)) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (range && source_offset.v >= range->source_range.start.v && source_offset.v <= range->source_range.end.v) {
                return DocumentPosition{
                    block.id,
                    source_offset.v <= range->source_range.start.v ? 0u : 1u,
                    source_offset.v <= range->source_range.start.v ? TextAffinity::Upstream : TextAffinity::Downstream};
            }
        }
        if (auto position = position_in_blocks(document, block.quote_children, source_offset)) return position;
        if (!block.quote_children.empty()) {
            const auto* range = document.source_map.find_node_by_id(block.id);
            if (range && source_offset.v >= range->source_range.start.v && source_offset.v <= range->source_range.end.v) {
                if (auto position = position_near_blocks(document, block.quote_children, source_offset)) return position;
            }
        }
        for (const auto& item : block.list_items) {
            if (auto position = position_in_blocks(document, item.children, source_offset)) return position;
            const auto* range = document.source_map.find_node_by_id(item.id);
            if (range && source_offset.v >= range->source_range.start.v && source_offset.v <= range->source_range.end.v) {
                if (auto position = position_near_blocks(document, item.children, source_offset)) return position;
            }
        }
        for (const auto& item : block.task_items) {
            if (auto position = position_in_blocks(document, item.children, source_offset)) return position;
            const auto* range = document.source_map.find_node_by_id(item.id);
            if (range && source_offset.v >= range->source_range.start.v && source_offset.v <= range->source_range.end.v) {
                if (auto position = position_near_blocks(document, item.children, source_offset)) return position;
            }
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
    if (position.inline_node_id.v != 0) {
        const auto* inline_range = document.source_map.find_node_by_id(position.inline_node_id);
        if (!inline_range) return std::nullopt;
        if (position.part == DocumentPositionPart::OpeningMarker) {
            return CharOffset(inline_range->source_range.start.v + position.part_offset);
        }
        if (position.part == DocumentPositionPart::ClosingMarker) {
            return CharOffset(inline_range->content_range.end.v + position.part_offset);
        }
        return CharOffset(inline_range->content_range.start.v + position.part_offset);
    }
    const auto* paragraph = document_projection_detail::find_text_block(document.blocks, position.node_id);
    const auto* range = document.source_map.find_node_by_id(position.node_id);
    if (range) {
        if (position.part == DocumentPositionPart::OpeningMarker) {
            const auto marker_length = range->content_range.start.v - range->source_range.start.v;
            return CharOffset(range->source_range.start.v + (std::min)(position.part_offset, marker_length));
        }
        if (position.part == DocumentPositionPart::ClosingMarker) {
            const auto marker_length = range->source_range.end.v - range->content_range.end.v;
            return CharOffset(range->content_range.end.v + (std::min)(position.part_offset, marker_length));
        }
        std::function<const BlockNode*(const BlockVec&)> find_atomic = [&](const BlockVec& blocks) -> const BlockNode* {
            for (const auto& block : blocks) {
                if (block.id == position.node_id && document_projection_detail::atomic_editable_kind(block.kind)) return &block;
                if (const auto* nested = find_atomic(block.quote_children)) return nested;
                for (const auto& item : block.list_items) if (const auto* nested = find_atomic(item.children)) return nested;
                for (const auto& item : block.task_items) if (const auto* nested = find_atomic(item.children)) return nested;
            }
            return nullptr;
        };
        if (find_atomic(document.blocks)) return position.offset == 0 ? range->source_range.start : range->source_range.end;
    }
    if (paragraph && range) {
        const auto logical_length = block_inline_text_content(paragraph->children).size();
        if (paragraph->children.empty()) return range->content_range.start;
        if (auto source = document_projection_detail::source_offset_from_logical(
                document, paragraph->children, (std::min)(position.offset, logical_length), range->content_range.start, position.affinity)) return source;
        return range->content_range.end;
    }
    std::function<const BlockNode*(const BlockVec&)> find_math = [&](const BlockVec& blocks) -> const BlockNode* {
        for (const auto& block : blocks) {
            if (block.id == position.node_id && block.kind == BlockKind::MathBlock) return &block;
            if (const auto* nested = find_math(block.quote_children)) return nested;
            for (const auto& item : block.list_items) if (const auto* nested = find_math(item.children)) return nested;
            for (const auto& item : block.task_items) if (const auto* nested = find_math(item.children)) return nested;
        }
        return nullptr;
    };
    if (const auto* math = find_math(document.blocks)) {
        const auto* math_range = document.source_map.find_node_by_id(math->id);
        if (!math_range) return std::nullopt;
        return CharOffset(math_range->content_range.start.v + (std::min)(position.offset, math->tex.size()));
    }
    std::function<const TableCell*(const BlockVec&)> find_cell = [&](const BlockVec& blocks) -> const TableCell* {
        for (const auto& block : blocks) {
            for (const auto& cell : block.table_header) if (cell.id == position.node_id) return &cell;
            for (const auto& row : block.table_rows) for (const auto& cell : row.cells) if (cell.id == position.node_id) return &cell;
            if (const auto* nested = find_cell(block.quote_children)) return nested;
            for (const auto& item : block.list_items) if (const auto* nested = find_cell(item.children)) return nested;
            for (const auto& item : block.task_items) if (const auto* nested = find_cell(item.children)) return nested;
        }
        return nullptr;
    };
    if (const auto* cell = find_cell(document.blocks)) {
        const auto* cell_range = document.source_map.find_node_by_id(cell->id);
        if (!cell_range) return std::nullopt;
        const auto logical_length = block_inline_text_content(cell->children).size();
        if (cell->children.empty()) return cell_range->content_range.start;
        if (auto source = document_projection_detail::source_offset_from_logical(
                document, cell->children, (std::min)(position.offset, logical_length), cell_range->content_range.start, position.affinity)) return source;
        return cell_range->content_range.end;
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

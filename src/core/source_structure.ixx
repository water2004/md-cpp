export module elmd.core.source_structure;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.document;
import elmd.core.source_map;

export namespace elmd {

enum class SourceBlockKind {
    Semantic,
    Blank,
};

struct SourceBlockSpan {
    SourceBlockKind kind = SourceBlockKind::Semantic;
    CharRange source_range;
    CharRange content_range;
    std::optional<std::size_t> document_block_index;
    std::optional<NodeId> node_id;
};

struct SourceStructure {
    std::vector<SourceBlockSpan> blocks;
    std::vector<CharRange> separators;
};

inline SourceStructure build_source_structure(const EditorDocument& document) {
    SourceStructure structure;
    structure.blocks.reserve(document.root.children.size());
    for (std::size_t index = 0; index < document.root.children.size(); ++index) {
        const auto& block = document.root.children[index];
        const auto* range = document.source_map.find_node_by_id(block.id);
        if (!range) continue;
        SourceBlockSpan span;
        span.kind = block.kind == BlockKind::Paragraph && block.inline_content.source.empty()
            ? SourceBlockKind::Blank : SourceBlockKind::Semantic;
        span.source_range = range->source_range;
        span.content_range = range->content_range;
        span.document_block_index = index;
        span.node_id = block.id;
        if (!structure.blocks.empty()) {
            const auto previous_end = structure.blocks.back().source_range.end;
            if (previous_end < span.source_range.start) {
                structure.separators.emplace_back(previous_end, span.source_range.start);
            }
        }
        structure.blocks.push_back(std::move(span));
    }
    return structure;
}

inline const SourceBlockSpan* source_blank_at(const SourceStructure& structure, CharOffset offset) {
    for (const auto& block : structure.blocks) {
        if (block.kind != SourceBlockKind::Blank) continue;
        if (block.content_range.contains(offset) || block.content_range.start == offset) return &block;
    }
    return nullptr;
}

inline const SourceBlockSpan* source_semantic_at(const SourceStructure& structure, CharOffset offset) {
    for (const auto& block : structure.blocks) {
        if (block.kind != SourceBlockKind::Semantic) continue;
        if (block.content_range.start <= offset && offset <= block.content_range.end) return &block;
    }
    for (const auto& block : structure.blocks) {
        if (block.kind == SourceBlockKind::Semantic && block.source_range.contains(offset)) return &block;
    }
    return nullptr;
}

}

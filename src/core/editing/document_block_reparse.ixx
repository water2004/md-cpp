// elmd.core.document_block_reparse — local block-structure recognition after
// edits to a direct block's authoritative source. This reparses one block
// fragment only; it never serializes or reparses the complete document.
export module elmd.core.document_block_reparse;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit_support;
import elmd.core.document_text;
import elmd.core.ids;
import elmd.core.parser;
import elmd.core.serializer;
import elmd.core.text_edit;

export namespace elmd::document_edit_detail {

namespace block_reparse_detail {

inline const BlockNode* fragment_block(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if (block.id == id) return &block;
        if (const auto* found = fragment_block(block.children, id)) return found;
    }
    return nullptr;
}

inline bool contains_raw_block(const BlockNode& block) {
    if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) return true;
    return std::ranges::any_of(block.children, [](const auto& child) {
        return contains_raw_block(child);
    });
}

inline bool contains_raw_block(const BlockVec& blocks) {
    return std::ranges::any_of(blocks, [](const auto& block) {
        return contains_raw_block(block);
    });
}

inline bool same_raw_projection(const BlockNode& current, const BlockVec& parsed) {
    if (parsed.size() != 1) return false;
    const auto& candidate = parsed.front();
    if (candidate.kind != current.kind || candidate.block_source.source() != current.block_source.source()) {
        return false;
    }
    if (candidate.block_source.tree().kind != current.block_source.tree().kind) return false;
    if (candidate.kind == BlockKind::MathBlock
        && candidate.special().math_delim != current.special().math_delim) return false;
    return true;
}

inline bool range_overlaps(SourceRange left, SourceRange right) {
    if (left.empty()) return right.covers(left.start);
    return left.start < right.end && right.start < left.end;
}

inline bool edit_touches_raw_structure(
    const BlockNode& current,
    const AppliedSourceEdit& edit) {
    if (edit.forward.container_id != current.id) return false;
    auto previous_source = current.block_source.source();
    apply_text_edit(previous_source, edit.inverse);
    const auto previous = parse_block_source(
        previous_source,
        current.block_source.tree().kind);
    return std::ranges::any_of(previous.tokens, [&](const auto& token) {
        const auto structural = token.kind == BlockSourceTokenKind::OpeningMarker
            || token.kind == BlockSourceTokenKind::Indentation;
        return structural && range_overlaps(edit.forward.range, token.source_range);
    });
}

struct TargetCandidate {
    NodeId parser_id{};
    std::size_t local_offset = 0;
    std::size_t distance = (std::numeric_limits<std::size_t>::max)();
    std::size_t physical_length = (std::numeric_limits<std::size_t>::max)();
};

inline std::optional<TargetCandidate> target_candidate(
    const ParsedBlockFragment& fragment,
    std::size_t physical_offset) {
    std::optional<TargetCandidate> best;
    for (const auto& range : fragment.source_ranges) {
        const auto* block = fragment_block(fragment.blocks, range.node_id);
        if (!block) continue;
        const auto* inline_document = editable_inline_document(*block);
        const auto* raw_source = editable_raw_block_source(*block);
        if (!inline_document && !raw_source) continue;

        const auto physical = raw_source ? range.source_range : range.content_range;
        const auto local_length = raw_source ? raw_source->size() : inline_document->source.size();
        std::size_t distance = 0;
        if (physical_offset < physical.start) distance = physical.start - physical_offset;
        else if (physical_offset > physical.end) distance = physical_offset - physical.end;
        const auto clamped = (std::clamp)(physical_offset, physical.start, physical.end);
        const auto local_offset = (std::min)(clamped - physical.start, local_length);
        const auto candidate = TargetCandidate{
            range.node_id,
            local_offset,
            distance,
            physical.length(),
        };
        if (!best || candidate.distance < best->distance
            || (candidate.distance == best->distance
                && candidate.physical_length < best->physical_length)) {
            best = candidate;
        }
    }
    return best;
}

inline void assign_fresh_ids(
    BlockNode& block,
    NodeId selected_parser_id,
    NodeId preserved_id,
    const EditorDocument& owner,
    NodeAllocator& allocator) {
    const auto parser_id = block.id;
    block.id = parser_id == selected_parser_id ? preserved_id : allocator.allocate();
    if (auto* inline_document = editable_inline_document(block)) {
        reparse(*inline_document, owner, allocator);
    }
    for (auto& child : block.children) {
        assign_fresh_ids(child, selected_parser_id, preserved_id, owner, allocator);
    }
}

inline BlockNode exact_paragraph(
    std::u32string source,
    NodeId id,
    std::optional<std::u32string> separator_before,
    const EditorDocument& owner,
    NodeAllocator& allocator) {
    BlockNode paragraph;
    paragraph.id = id;
    paragraph.kind = BlockKind::Paragraph;
    paragraph.inline_content = make_inline(std::move(source), owner, allocator);
    paragraph.separator_before = std::move(separator_before);
    return paragraph;
}

} // namespace block_reparse_detail

// Reclassify an edited raw block when its marker syntax changes. A Paragraph
// is reconsidered only when its local source already spans physical lines and
// the block parser finds code/math structure; ordinary per-character typing
// therefore still uses the Enter input rule and its auto-closing behavior.
inline std::optional<RecordedBlockEdit> reparse_edited_direct_block(
    EditorDocument& document,
    TextPosition position,
    NodeAllocator& allocator,
    const AppliedSourceEdit* source_edit = nullptr) {
    auto* current = find_document_block(document, position.container_id);
    if (!current) return std::nullopt;

    const auto is_raw = current->kind == BlockKind::CodeBlock
        || current->kind == BlockKind::MathBlock;
    const auto is_multiline_paragraph = current->kind == BlockKind::Paragraph
        && current->inline_content.source.find(U'\n') != std::u32string::npos;
    if (!is_raw && !is_multiline_paragraph) return std::nullopt;
    if (is_raw && source_edit
        && !block_reparse_detail::edit_touches_raw_structure(*current, *source_edit)) {
        return std::nullopt;
    }
    auto path = document_block_path(document, position.container_id);
    if (!path || path->empty()) return std::nullopt;

    const auto source = is_raw
        ? current->block_source.source()
        : current->inline_content.source;
    auto parsed = parse_block_fragment(source, document.dialect);
    if (is_raw && block_reparse_detail::same_raw_projection(*current, parsed.blocks)) {
        return std::nullopt;
    }
    if (!is_raw && !block_reparse_detail::contains_raw_block(parsed.blocks)) {
        return std::nullopt;
    }

    auto target = block_reparse_detail::target_candidate(parsed, position.source_offset);
    // A character edit may reclassify one direct block, but it is not an
    // implicit multi-block split command. Enter remains the operation that
    // creates sibling block boundaries.
    const auto exact = parsed.blocks.size() == 1
        && serializer_detail::serialize_blocks(parsed.blocks).text == source;
    if (!exact || !target) {
        if (!is_raw) return std::nullopt;
        parsed.blocks.clear();
        parsed.source_ranges.clear();
        parsed.blocks.push_back(block_reparse_detail::exact_paragraph(
            source,
            current->id,
            current->separator_before,
            document,
            allocator));
        target = block_reparse_detail::TargetCandidate{
            current->id,
            (std::min)(position.source_offset, source.size()),
            0,
            source.size(),
        };
    } else {
        parsed.blocks.front().separator_before = current->separator_before;
        for (auto& block : parsed.blocks) {
            block_reparse_detail::assign_fresh_ids(
                block,
                target->parser_id,
                current->id,
                document,
                allocator);
        }
    }

    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || index >= parent->children.size()) return std::nullopt;
    const auto parent_id = parent->id;

    RecordedBlockEdit result;
    result.target = {
        current->id,
        target->local_offset,
        TextAffinity::Downstream,
    };

    DocumentTreeEdit remove;
    remove.kind = DocumentTreeEditKind::Remove;
    remove.parent_id = parent_id;
    remove.index = index;
    remove.before = parent->children[index];
    result.operations.emplace_back(std::move(remove));
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));

    for (std::size_t replacement_index = 0;
         replacement_index < parsed.blocks.size();
         ++replacement_index) {
        parent = find_block(document.root, parent_id);
        if (!parent) return std::nullopt;
        auto replacement = std::move(parsed.blocks[replacement_index]);
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = parent_id;
        insert.index = index + replacement_index;
        insert.after = replacement;
        result.operations.emplace_back(std::move(insert));
        if (!insert_block(*parent, index + replacement_index, std::move(replacement))) {
            return std::nullopt;
        }
    }
    return result;
}

} // namespace elmd::document_edit_detail

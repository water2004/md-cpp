// Shared source-edit primitives, allocation, and public edit command types.
export module elmd.core.document_edit_primitives;
export import elmd.core.document_transaction;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_ids;
import elmd.core.document_operation_apply;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.ids;
import elmd.core.text_edit;
import elmd.core.utf;

export namespace elmd {

enum class InlineFormat { Emphasis, Strong, Strikethrough, Code, Math };
enum class DocumentTableEdit {
    MoveCellNext, MoveCellPrevious,
    InsertRowAbove, InsertRowBelow, DeleteRow, MoveRowUp, MoveRowDown,
    InsertColumnLeft, InsertColumnRight, DeleteColumn, MoveColumnLeft, MoveColumnRight,
    SetColumnAlignment, Normalize, InsertRowAt, InsertColumnAt, MoveRowTo, MoveColumnTo,
};
enum class DocumentMove { Left, Right, Up, Down, LineStart, LineEnd, DocumentStart, DocumentEnd };

struct DocumentInvariantError { NodeId node_id{}; std::string message; };

namespace document_edit_detail {

class MutationRollback {
public:
    MutationRollback(
        EditorDocument& document,
        const std::vector<DocumentOperation>& operations,
        std::uint64_t revision_before)
        : document_(&document), operations_(&operations), revision_before_(revision_before) {}

    MutationRollback(const MutationRollback&) = delete;
    MutationRollback& operator=(const MutationRollback&) = delete;

    ~MutationRollback() {
        if (!active_) return;
        if (!apply_document_operations(*document_, *operations_, false)) std::terminate();
        document_->revision = revision_before_;
    }

    void commit() noexcept { active_ = false; }

private:
    EditorDocument* document_;
    const std::vector<DocumentOperation>* operations_;
    std::uint64_t revision_before_;
    bool active_ = true;
};

inline bool text_block(BlockKind kind) {
    return kind == BlockKind::Paragraph
        || kind == BlockKind::Heading
        || kind == BlockKind::CalloutTitle;
}
inline bool atomic_block(BlockKind kind) {
    return kind == BlockKind::ImageBlock || kind == BlockKind::Toc || kind == BlockKind::Frontmatter
        || kind == BlockKind::ThematicBreak || kind == BlockKind::LinkDefinition
        || kind == BlockKind::UnsupportedMarkup || kind == BlockKind::Extension;
}

inline std::optional<std::size_t> local_position_length(const BlockNode& block) {
    if (const auto* document = editable_inline_document(block)) return document->source.size();
    if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
        return block.block_source.source().size();
    }
    if (block.kind == BlockKind::Frontmatter || block.kind == BlockKind::LinkDefinition
        || block.kind == BlockKind::UnsupportedMarkup) return utf8_to_cps(block.special().raw).size();
    if (block.kind == BlockKind::ImageBlock || block.kind == BlockKind::Toc
        || block.kind == BlockKind::ThematicBreak || block.kind == BlockKind::Extension) return 1;
    return std::nullopt;
}

inline std::optional<TextPosition> first_editable_position(const BlockNode& block) {
    if (local_position_length(block)) {
        return TextPosition{block.id, 0, TextAffinity::Downstream};
    }
    for (const auto& child : block.children) {
        if (auto position = first_editable_position(child)) return position;
    }
    return std::nullopt;
}

inline std::optional<TextPosition> last_editable_position(const BlockNode& block) {
    for (auto child = block.children.rbegin(); child != block.children.rend(); ++child) {
        if (auto position = last_editable_position(*child)) return position;
    }
    if (auto length = local_position_length(block)) return TextPosition{block.id, *length, TextAffinity::Upstream};
    return std::nullopt;
}

struct NodeAllocator {
    EditorDocument* document = nullptr;
    explicit NodeAllocator(EditorDocument& owner) : document(&owner) {
        ensure_document_node_id_cursor(owner);
    }
    NodeId allocate() { return allocate_document_node_id(*document); }
};

inline InlineParseContext parse_context(const EditorDocument& document, NodeAllocator& allocator) {
    InlineParseContext context;
    context.dialect = document.dialect;
    context.allocate_id = [&allocator] { return allocator.allocate(); };
    return context;
}

inline void reparse(InlineDocument& document, const EditorDocument& owner, NodeAllocator& allocator) {
    document.tree = parse_inline(document.source, parse_context(owner, allocator));
}

inline InlineDocument make_inline(std::u32string source, const EditorDocument& owner, NodeAllocator& allocator) {
    InlineDocument document;
    document.source = std::move(source);
    reparse(document, owner, allocator);
    return document;
}

inline InlineDocument* find_inline_owner(BlockNode& root, NodeId id) {
    auto* block = elmd::find_block(root, id);
    return block ? editable_inline_document(*block) : nullptr;
}
inline const InlineDocument* find_inline_owner(const BlockNode& root, NodeId id) {
    const auto* block = elmd::find_block(root, id);
    return block ? editable_inline_document(*block) : nullptr;
}
inline InlineDocument* find_inline_owner(EditorDocument& document, NodeId id) {
    auto* block = find_document_block(document, id);
    return block ? editable_inline_document(*block) : nullptr;
}
inline const InlineDocument* find_inline_owner(const EditorDocument& document, NodeId id) {
    const auto* block = find_document_block(document, id);
    return block ? editable_inline_document(*block) : nullptr;
}

inline void assign_missing_ids(InlineCstNodes& nodes, NodeAllocator& allocator) {
    for (auto& node : nodes) {
        if (node.id.v == 0) node.id = allocator.allocate();
        assign_missing_ids(node.children, allocator);
    }
}
inline void assign_missing_ids(InlineDocument& document, const EditorDocument& owner, NodeAllocator& allocator) {
    if (!tokens_partition_source(document.tree, document.source.size())
        || !roots_partition_source(document.tree, document.source.size())) reparse(document, owner, allocator);
    assign_missing_ids(document.tree.nodes, allocator);
    for (auto& token : document.tree.tokens) if (token.id.v == 0) token.id = allocator.allocate();
}
inline void assign_missing_ids(BlockNode& block, const EditorDocument& owner, NodeAllocator& allocator) {
    if (block.id.v == 0) block.id = allocator.allocate();
    if (auto* document = editable_inline_document(block)) assign_missing_ids(*document, owner, allocator);
    for (auto& child : block.children) assign_missing_ids(child, owner, allocator);
}

inline BlockNode empty_paragraph(NodeAllocator& allocator, const EditorDocument& owner) {
    BlockNode paragraph; paragraph.id = allocator.allocate(); paragraph.kind = BlockKind::Paragraph;
    paragraph.inline_content = make_inline({}, owner, allocator);
    return paragraph;
}

inline void normalize_blocks(BlockVec& blocks, const EditorDocument& owner, NodeAllocator& allocator) {
    for (auto& block : blocks) {
        assign_missing_ids(block, owner, allocator);
        normalize_blocks(block.children, owner, allocator);
    }
}

inline std::optional<AppliedSourceEdit> edit_inline(
    EditorDocument& document,
    NodeId owner_id,
    SourceRange range,
    std::u32string replacement,
    NodeAllocator& allocator) {
    auto* inline_document = find_inline_owner(document, owner_id);
    if (!inline_document || !range.valid_for(inline_document->source.size())) return std::nullopt;
    return apply_inline_source_edit(
        owner_id,
        *inline_document,
        TextEdit{owner_id, range, std::move(replacement)},
        parse_context(document, allocator));
}

inline std::optional<AppliedSourceEdit> edit_block_source(
    EditorDocument& document,
    BlockNode& block,
    SourceRange range,
    std::u32string replacement,
    NodeAllocator& allocator) {
    if (auto* inline_document = editable_inline_document(block)) {
        if (!range.valid_for(inline_document->source.size())) return std::nullopt;
        return apply_inline_source_edit(
            block.id,
            *inline_document,
            TextEdit{block.id, range, std::move(replacement)},
            parse_context(document, allocator));
    }
    auto* source = editable_raw_block_source(block);
    if (!source || !range.valid_for(source->size())) return std::nullopt;
    auto removed = source->substr(range.start, range.length());
    TextEdit forward{block.id, range, std::move(replacement)};
    TextEdit inverse{
        block.id,
        {range.start, range.start + forward.replacement.size()},
        std::move(removed)};
    apply_text_edit(*source, forward);
    if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
        reparse_block_source(block.block_source);
    }
    return AppliedSourceEdit{std::move(forward), std::move(inverse)};
}

inline std::optional<AppliedSourceEdit> edit_block_source(
    EditorDocument& document,
    NodeId owner_id,
    SourceRange range,
    std::u32string replacement,
    NodeAllocator& allocator) {
    auto* block = find_document_block(document, owner_id);
    if (!block) return std::nullopt;
    return edit_block_source(
        document, *block, range, std::move(replacement), allocator);
}

struct InsertResult {
    std::size_t offset = 0;
    TextAffinity affinity = TextAffinity::Downstream;
    std::optional<AppliedSourceEdit> source_edit;
};
inline std::optional<InsertResult> insert_text(
    EditorDocument& document,
    TextPosition position,
    std::u32string_view text,
    NodeAllocator& allocator) {
    auto* block = find_document_block(document, position.container_id);
    if (!block) return std::nullopt;
    const auto* inline_document = editable_inline_document(*block);
    const auto* raw_source = editable_raw_block_source(*block);
    if (!inline_document && !raw_source) return std::nullopt;
    const auto length = inline_document ? inline_document->source.size() : raw_source->size();
    const auto offset = (std::min)(position.source_offset, length);
    auto applied = edit_block_source(
        document,
        *block,
        {offset, offset},
        std::u32string(text),
        allocator);
    if (!applied) return std::nullopt;
    return InsertResult{offset + text.size(), TextAffinity::Downstream, std::move(applied)};
}

inline std::optional<std::size_t> editable_length(const EditorDocument& document, NodeId id) {
    if (const auto* inline_document = find_inline_owner(document, id)) return inline_document->source.size();
    if (const auto* block = find_document_block(document, id)) {
        if (block->kind == BlockKind::CodeBlock || block->kind == BlockKind::MathBlock) {
            return block->block_source.source().size();
        }
        if (atomic_block(block->kind)) return 1;
    }
    return std::nullopt;
}

inline std::optional<AppliedSourceEdit> erase_text(
    EditorDocument& document,
    NodeId id,
    SourceRange range,
    NodeAllocator& allocator) {
    return edit_block_source(document, id, range, {}, allocator);
}

enum class ListStyle { Bullet, Ordered, Task };

} // namespace document_edit_detail

} // namespace elmd

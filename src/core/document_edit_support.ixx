export module elmd.core.document_edit_support;
export import elmd.core.document_transaction;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
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

inline bool text_block(BlockKind kind) { return kind == BlockKind::Paragraph || kind == BlockKind::Heading; }
inline bool atomic_block(BlockKind kind) {
    return kind == BlockKind::ImageBlock || kind == BlockKind::Toc || kind == BlockKind::Frontmatter
        || kind == BlockKind::ThematicBreak || kind == BlockKind::LinkDefinition
        || kind == BlockKind::UnsupportedMarkup || kind == BlockKind::Extension;
}

inline std::optional<std::size_t> local_position_length(const BlockNode& block) {
    if (const auto* document = editable_inline_document(block)) return document->source.size();
    if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
        return block.block_source.source.size();
    }
    if (block.kind == BlockKind::Frontmatter || block.kind == BlockKind::LinkDefinition
        || block.kind == BlockKind::UnsupportedMarkup) return utf8_to_cps(block.raw).size();
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

inline void scan_inline_ids(const InlineCstNodes& nodes, std::uint64_t& maximum) {
    for (const auto& node : nodes) {
        maximum = (std::max)(maximum, node.id.v);
        scan_inline_ids(node.children, maximum);
    }
}
inline void scan_inline_ids(const InlineDocument& document, std::uint64_t& maximum) {
    scan_inline_ids(document.tree.nodes, maximum);
    for (const auto& token : document.tree.tokens) maximum = (std::max)(maximum, token.id.v);
}
inline void scan_block_ids(const BlockNode& block, std::uint64_t& maximum) {
    maximum = (std::max)(maximum, block.id.v);
    if (const auto* document = editable_inline_document(block)) scan_inline_ids(*document, maximum);
    for (const auto& child : block.children) scan_block_ids(child, maximum);
}

struct NodeAllocator {
    std::uint64_t next = 1;
    explicit NodeAllocator(const EditorDocument& document) {
        std::uint64_t maximum = document.root.id.v;
        for (const auto& block : document.root.children) scan_block_ids(block, maximum);
        next = maximum + 1;
    }
    NodeId allocate() { return NodeId{next++}; }
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

inline InlineDocument* find_inline_owner(BlockVec& blocks, NodeId id) {
    for (auto& block : blocks) {
        if (block.id == id) return editable_inline_document(block);
        if (auto* found = find_inline_owner(block.children, id)) return found;
    }
    return nullptr;
}
inline const InlineDocument* find_inline_owner(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if (block.id == id) return editable_inline_document(block);
        if (const auto* found = find_inline_owner(block.children, id)) return found;
    }
    return nullptr;
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
    auto* inline_document = find_inline_owner(document.root.children, owner_id);
    if (!inline_document || !range.valid_for(inline_document->source.size())) return std::nullopt;
    return apply_inline_source_edit(
        owner_id,
        *inline_document,
        TextEdit{owner_id, range, std::move(replacement)},
        parse_context(document, allocator));
}

inline std::optional<AppliedSourceEdit> edit_block_source(
    EditorDocument& document,
    NodeId owner_id,
    SourceRange range,
    std::u32string replacement,
    NodeAllocator& allocator) {
    auto* block = elmd::find_block(document.root, owner_id);
    if (!block) return std::nullopt;
    if (editable_inline_document(*block)) {
        return edit_inline(document, owner_id, range, std::move(replacement), allocator);
    }
    auto* source = editable_raw_block_source(*block);
    if (!source || !range.valid_for(source->size())) return std::nullopt;
    auto removed = source->substr(range.start, range.length());
    TextEdit forward{owner_id, range, std::move(replacement)};
    TextEdit inverse{
        owner_id,
        {range.start, range.start + forward.replacement.size()},
        std::move(removed)};
    apply_text_edit(*source, forward);
    if (block->kind == BlockKind::CodeBlock || block->kind == BlockKind::MathBlock) {
        reparse_block_source(block->block_source);
    }
    return AppliedSourceEdit{std::move(forward), std::move(inverse)};
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
    auto* block = elmd::find_block(document.root, position.container_id);
    if (!block) return std::nullopt;
    const auto* inline_document = editable_inline_document(*block);
    const auto* raw_source = editable_raw_block_source(*block);
    if (!inline_document && !raw_source) return std::nullopt;
    const auto length = inline_document ? inline_document->source.size() : raw_source->size();
    const auto offset = (std::min)(position.source_offset, length);
    auto applied = edit_block_source(
        document,
        position.container_id,
        {offset, offset},
        std::u32string(text),
        allocator);
    if (!applied) return std::nullopt;
    return InsertResult{offset + text.size(), TextAffinity::Downstream, std::move(applied)};
}

inline std::optional<std::size_t> editable_length(const EditorDocument& document, NodeId id) {
    if (const auto* inline_document = find_inline_owner(document.root.children, id)) return inline_document->source.size();
    if (const auto* block = elmd::find_block(document.root, id)) {
        if (block->kind == BlockKind::CodeBlock || block->kind == BlockKind::MathBlock) {
            return block->block_source.source.size();
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

struct RecordedBlockEdit {
    TextPosition target;
    std::vector<DocumentOperation> operations;
};

inline void append_source_operation(
    std::vector<DocumentOperation>& operations,
    AppliedSourceEdit edit) {
    operations.emplace_back(DocumentTextOperation{
        std::move(edit.forward),
        std::move(edit.inverse),
    });
}

inline std::optional<RecordedBlockEdit> split_direct(
    EditorDocument& document,
    NodeId id,
    std::size_t offset,
    NodeAllocator& allocator) {
    auto path = block_path(document.root, id);
    if (!path || path->empty()) return std::nullopt;
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    auto* block = block_at_path(document.root, *path);
    if (!parent || !block || index >= parent->children.size()) return std::nullopt;

    RecordedBlockEdit result;
    if (block->kind == BlockKind::Callout && block->callout_title) {
        offset = (std::min)(offset, block->callout_title->source.size());
        auto right_source = block->callout_title->source.substr(offset);
        if (offset == 0) {
            DocumentTreeEdit update;
            update.kind = DocumentTreeEditKind::UpdatePayload;
            update.before = document_transaction_detail::payload_shell(*block);
            block->callout_title.reset();
            update.after = document_transaction_detail::payload_shell(*block);
            result.operations.emplace_back(std::move(update));
        } else if (offset < block->callout_title->source.size()) {
            auto edit = edit_inline(
                document,
                id,
                {offset, block->callout_title->source.size()},
                {},
                allocator);
            if (!edit) return std::nullopt;
            append_source_operation(result.operations, std::move(*edit));
            block = find_block(document.root, id);
            if (!block) return std::nullopt;
        }

        BlockNode right;
        right.id = allocator.allocate();
        right.kind = BlockKind::Paragraph;
        right.inline_content = make_inline(std::move(right_source), document, allocator);
        result.target = TextPosition{right.id, 0, TextAffinity::Downstream};
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = block->id;
        insert.index = 0;
        insert.after = right;
        result.operations.emplace_back(std::move(insert));
        block->children.insert(block->children.begin(), std::move(right));
        return result;
    }

    if (!text_block(block->kind)) return std::nullopt;
    offset = (std::min)(offset, block->inline_content.source.size());
    auto right_source = block->inline_content.source.substr(offset);
    if (offset < block->inline_content.source.size()) {
        auto edit = edit_inline(
            document,
            id,
            {offset, block->inline_content.source.size()},
            {},
            allocator);
        if (!edit) return std::nullopt;
        append_source_operation(result.operations, std::move(*edit));
    }

    BlockNode right;
    right.id = allocator.allocate();
    right.kind = BlockKind::Paragraph;
    right.inline_content = make_inline(std::move(right_source), document, allocator);
    result.target = TextPosition{right.id, 0, TextAffinity::Downstream};
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = parent->id;
    insert.index = index + 1;
    insert.after = right;
    result.operations.emplace_back(std::move(insert));
    parent->children.insert(
        parent->children.begin() + static_cast<std::ptrdiff_t>(index + 1),
        std::move(right));
    return result;
}

inline std::optional<RecordedBlockEdit> join_parent_inline_owner(
    EditorDocument& document,
    NodeId child_id,
    NodeAllocator& allocator) {
    auto path = block_path(document.root, child_id);
    if (!path || path->size() < 2 || path->back() != 0) return std::nullopt;
    auto parent_path = *path;
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || parent->children.empty()) return std::nullopt;
    auto* parent_inline = editable_inline_document(*parent);
    auto* child_inline = editable_inline_document(parent->children.front());
    if (!parent_inline || !child_inline) return std::nullopt;

    const auto offset = parent_inline->source.size();
    auto child_source = child_inline->source;
    RecordedBlockEdit result;
    result.target = TextPosition{
        parent->id,
        offset,
        offset == 0 ? TextAffinity::Downstream : TextAffinity::Upstream};
    if (!child_source.empty()) {
        auto edit = edit_inline(document, parent->id, {offset, offset}, std::move(child_source), allocator);
        if (!edit) return std::nullopt;
        append_source_operation(result.operations, std::move(*edit));
        parent = find_block(document.root, parent->id);
        if (!parent || parent->children.empty()) return std::nullopt;
    }
    DocumentTreeEdit remove;
    remove.kind = DocumentTreeEditKind::Remove;
    remove.parent_id = parent->id;
    remove.index = 0;
    remove.before = parent->children.front();
    result.operations.emplace_back(std::move(remove));
    parent->children.erase(parent->children.begin());
    return result;
}

inline std::optional<RecordedBlockEdit> join_first_child_into_inline_owner(
    EditorDocument& document,
    NodeId parent_id,
    NodeAllocator& allocator) {
    auto* parent = find_block(document.root, parent_id);
    if (!parent || parent->children.empty()) return std::nullopt;
    auto* parent_inline = editable_inline_document(*parent);
    auto* child_inline = editable_inline_document(parent->children.front());
    if (!parent_inline || !child_inline) return std::nullopt;

    const auto offset = parent_inline->source.size();
    auto child_source = child_inline->source;
    RecordedBlockEdit result;
    result.target = TextPosition{parent->id, offset, TextAffinity::Downstream};
    if (!child_source.empty()) {
        auto edit = edit_inline(document, parent->id, {offset, offset}, std::move(child_source), allocator);
        if (!edit) return std::nullopt;
        append_source_operation(result.operations, std::move(*edit));
        parent = find_block(document.root, parent_id);
        if (!parent || parent->children.empty()) return std::nullopt;
    }
    DocumentTreeEdit remove;
    remove.kind = DocumentTreeEditKind::Remove;
    remove.parent_id = parent->id;
    remove.index = 0;
    remove.before = parent->children.front();
    result.operations.emplace_back(std::move(remove));
    parent->children.erase(parent->children.begin());
    return result;
}

inline std::optional<RecordedBlockEdit> exit_empty_indented_code(
    EditorDocument& document,
    TextPosition position,
    NodeAllocator& allocator) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto* block = block_at_path(document.root, *path);
    if (!block || block->kind != BlockKind::CodeBlock || !block->code_indented) return std::nullopt;

    const auto& source = block->block_source.source;
    const auto offset = (std::min)(position.source_offset, source.size());
    auto line_start = offset == 0 ? std::u32string::npos : source.rfind(U'\n', offset - 1);
    line_start = line_start == std::u32string::npos ? 0 : line_start + 1;
    auto line_end = source.find(U'\n', offset);
    if (line_end == std::u32string::npos) line_end = source.size();
    for (auto index = line_start; index < line_end; ++index) {
        if (source[index] != U' ' && source[index] != U'\t') return std::nullopt;
    }

    // The blank line is the exit trigger, not content that should survive in
    // either code block. Remove its preceding line separator as well so the
    // leading block does not retain a visually empty trailing line.
    const auto before_end = line_start > 0 && source[line_start - 1] == U'\n'
        ? line_start - 1
        : line_start;
    auto before = source.substr(0, before_end);
    auto after_start = line_end < source.size() ? line_end + 1 : line_end;
    auto after = source.substr(after_start);
    const auto source_size = source.size();
    auto parent_path = *path;
    const auto block_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || block_index >= parent->children.size()) return std::nullopt;

    auto paragraph = empty_paragraph(allocator, document);
    RecordedBlockEdit result;
    result.target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    const auto parent_id = parent->id;
    if (before.empty()) {
        if (after.empty()) {
            DocumentTreeEdit remove;
            remove.kind = DocumentTreeEditKind::Remove;
            remove.parent_id = parent_id;
            remove.index = block_index;
            remove.before = parent->children[block_index];
            result.operations.emplace_back(std::move(remove));
            parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(block_index));
        } else {
            auto edit = edit_block_source(
                document,
                position.container_id,
                {0, after_start},
                {},
                allocator);
            if (!edit) return std::nullopt;
            append_source_operation(result.operations, std::move(*edit));
        }

        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = parent_id;
        insert.index = block_index;
        insert.after = paragraph;
        result.operations.emplace_back(std::move(insert));
        parent = find_block(document.root, parent_id);
        if (!parent || !insert_block(*parent, block_index, std::move(paragraph))) return std::nullopt;
        return result;
    }

    auto edit = edit_block_source(
        document,
        position.container_id,
        {before_end, source_size},
        {},
        allocator);
    if (!edit) return std::nullopt;
    append_source_operation(result.operations, std::move(*edit));

    DocumentTreeEdit insert_paragraph;
    insert_paragraph.kind = DocumentTreeEditKind::Insert;
    insert_paragraph.parent_id = parent_id;
    insert_paragraph.index = block_index + 1;
    insert_paragraph.after = paragraph;
    result.operations.emplace_back(std::move(insert_paragraph));
    parent = find_block(document.root, parent_id);
    if (!parent || !insert_block(*parent, block_index + 1, std::move(paragraph))) return std::nullopt;
    if (!after.empty()) {
        auto trailing = parent->children[block_index];
        trailing.id = allocator.allocate();
        trailing.block_source = make_block_source(std::move(after), BlockSourceKind::IndentedCode);
        DocumentTreeEdit insert_trailing;
        insert_trailing.kind = DocumentTreeEditKind::Insert;
        insert_trailing.parent_id = parent_id;
        insert_trailing.index = block_index + 2;
        insert_trailing.after = trailing;
        result.operations.emplace_back(std::move(insert_trailing));
        if (!insert_block(*parent, block_index + 2, std::move(trailing))) return std::nullopt;
    }
    return result;
}

inline std::optional<RecordedBlockEdit> exit_empty_block_quote(
    EditorDocument& document,
    TextPosition position,
    NodeAllocator& allocator) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto* selected = block_at_path(document.root, *path);
    if (!selected || selected->kind != BlockKind::Paragraph || !selected->inline_content.source.empty()) return std::nullopt;

    std::optional<std::size_t> quote_depth;
    for (std::size_t depth = path->size(); depth > 0; --depth) {
        BlockPath candidate(path->begin(), path->begin() + static_cast<std::ptrdiff_t>(depth));
        auto const* ancestor = block_at_path(document.root, candidate);
        if (ancestor && ancestor->kind == BlockKind::BlockQuote) { quote_depth = depth; break; }
    }
    if (!quote_depth || path->size() != *quote_depth + 1) return std::nullopt;

    BlockPath quote_path(path->begin(), path->begin() + static_cast<std::ptrdiff_t>(*quote_depth));
    auto parent_path = quote_path;
    const auto quote_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || quote_index >= parent->children.size()) return std::nullopt;
    auto& quote = parent->children[quote_index];
    const auto child_index = (*path)[*quote_depth];
    if (child_index >= quote.children.size()) return std::nullopt;

    // Delete the quote's empty content node. A fresh paragraph outside the
    // quote owns the caret; reparenting the trigger line would preserve it.
    auto paragraph = empty_paragraph(allocator, document);
    RecordedBlockEdit result;
    result.target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    const auto parent_id = parent->id;
    const auto quote_id = quote.id;
    const auto trailing_count = quote.children.size() - child_index - 1;

    DocumentTreeEdit insert_paragraph;
    insert_paragraph.kind = DocumentTreeEditKind::Insert;
    insert_paragraph.parent_id = parent_id;
    insert_paragraph.index = quote_index + 1;
    insert_paragraph.after = paragraph;
    result.operations.emplace_back(std::move(insert_paragraph));
    if (!insert_block(*parent, quote_index + 1, std::move(paragraph))) return std::nullopt;

    std::optional<NodeId> trailing_id;
    if (trailing_count > 0) {
        auto* current_quote = find_block(document.root, quote_id);
        if (!current_quote) return std::nullopt;
        auto trailing = document_transaction_detail::payload_shell(*current_quote);
        trailing.id = allocator.allocate();
        trailing_id = trailing.id;
        DocumentTreeEdit insert_trailing;
        insert_trailing.kind = DocumentTreeEditKind::Insert;
        insert_trailing.parent_id = parent_id;
        insert_trailing.index = quote_index + 2;
        insert_trailing.after = trailing;
        result.operations.emplace_back(std::move(insert_trailing));
        parent = find_block(document.root, parent_id);
        if (!parent || !insert_block(*parent, quote_index + 2, std::move(trailing))) return std::nullopt;

        for (std::size_t target_index = 0; target_index < trailing_count; ++target_index) {
            auto* source_quote = find_block(document.root, quote_id);
            auto* trailing_quote = find_block(document.root, *trailing_id);
            if (!source_quote || !trailing_quote || child_index + 1 >= source_quote->children.size()) {
                return std::nullopt;
            }
            DocumentTreeEdit move;
            move.kind = DocumentTreeEditKind::Move;
            move.parent_id = quote_id;
            move.index = child_index + 1;
            move.other_parent_id = *trailing_id;
            move.other_index = target_index;
            auto child = remove_block(*source_quote, child_index + 1);
            if (!child || !insert_block(*trailing_quote, target_index, std::move(*child))) return std::nullopt;
            result.operations.emplace_back(std::move(move));
        }
    }

    auto* current_quote = find_block(document.root, quote_id);
    if (!current_quote || child_index >= current_quote->children.size()) return std::nullopt;
    DocumentTreeEdit remove_trigger;
    remove_trigger.kind = DocumentTreeEditKind::Remove;
    remove_trigger.parent_id = quote_id;
    remove_trigger.index = child_index;
    remove_trigger.before = current_quote->children[child_index];
    result.operations.emplace_back(std::move(remove_trigger));
    current_quote->children.erase(
        current_quote->children.begin() + static_cast<std::ptrdiff_t>(child_index));

    if (current_quote->children.empty()) {
        parent = find_block(document.root, parent_id);
        if (!parent || quote_index >= parent->children.size()) return std::nullopt;
        DocumentTreeEdit remove_quote;
        remove_quote.kind = DocumentTreeEditKind::Remove;
        remove_quote.parent_id = parent_id;
        remove_quote.index = quote_index;
        remove_quote.before = parent->children[quote_index];
        result.operations.emplace_back(std::move(remove_quote));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(quote_index));
    }
    return result;
}

inline std::optional<RecordedBlockEdit> join_adjacent(
    EditorDocument& document,
    NodeId id,
    bool backward,
    NodeAllocator& allocator) {
    auto path = block_path(document.root, id);
    if (!path || path->empty()) return std::nullopt;
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || index >= parent->children.size() || !text_block(parent->children[index].kind)) {
        return std::nullopt;
    }
    auto& current = parent->children[index];
    const auto parent_id = parent->id;
    RecordedBlockEdit result;

    if (backward && index > 0 && text_block(parent->children[index - 1].kind)) {
        const auto owner = parent->children[index - 1].id;
        const auto offset = parent->children[index - 1].inline_content.source.size();
        auto source = current.inline_content.source;
        if (!source.empty()) {
            auto edit = edit_inline(document, owner, {offset, offset}, std::move(source), allocator);
            if (!edit) return std::nullopt;
            append_source_operation(result.operations, std::move(*edit));
        }
        parent = find_block(document.root, parent_id);
        if (!parent || index >= parent->children.size()) return std::nullopt;
        DocumentTreeEdit remove;
        remove.kind = DocumentTreeEditKind::Remove;
        remove.parent_id = parent_id;
        remove.index = index;
        remove.before = parent->children[index];
        result.operations.emplace_back(std::move(remove));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));
        result.target = TextPosition{
            owner,
            offset,
            offset == 0 ? TextAffinity::Downstream : TextAffinity::Upstream};
        return result;
    }
    if (backward && index > 0 && current.kind == BlockKind::Paragraph
        && current.inline_content.source.empty()) {
        if (auto previous = last_editable_position(parent->children[index - 1])) {
            result.target = *previous;
            DocumentTreeEdit remove;
            remove.kind = DocumentTreeEditKind::Remove;
            remove.parent_id = parent_id;
            remove.index = index;
            remove.before = current;
            result.operations.emplace_back(std::move(remove));
            parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));
            return result;
        }
    }
    if (!backward && index + 1 < parent->children.size()
        && text_block(parent->children[index + 1].kind)) {
        const auto owner = current.id;
        const auto offset = current.inline_content.source.size();
        auto source = parent->children[index + 1].inline_content.source;
        if (!source.empty()) {
            auto edit = edit_inline(document, owner, {offset, offset}, std::move(source), allocator);
            if (!edit) return std::nullopt;
            append_source_operation(result.operations, std::move(*edit));
        }
        parent = find_block(document.root, parent_id);
        if (!parent || index + 1 >= parent->children.size()) return std::nullopt;
        DocumentTreeEdit remove;
        remove.kind = DocumentTreeEditKind::Remove;
        remove.parent_id = parent_id;
        remove.index = index + 1;
        remove.before = parent->children[index + 1];
        result.operations.emplace_back(std::move(remove));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index + 1));
        result.target = TextPosition{owner, offset, TextAffinity::Downstream};
        return result;
    }
    if (!backward && index + 1 < parent->children.size()
        && current.kind == BlockKind::Paragraph && current.inline_content.source.empty()) {
        if (auto next = first_editable_position(parent->children[index + 1])) {
            result.target = *next;
            DocumentTreeEdit remove;
            remove.kind = DocumentTreeEditKind::Remove;
            remove.parent_id = parent_id;
            remove.index = index;
            remove.before = current;
            result.operations.emplace_back(std::move(remove));
            parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));
            return result;
        }
    }
    return std::nullopt;
}

inline std::optional<RecordedBlockEdit> remove_atomic(
    EditorDocument& document,
    NodeId id,
    NodeAllocator& allocator) {
    auto path = block_path(document.root, id);
    if (!path || path->empty()) return std::nullopt;
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || index >= parent->children.size() || !atomic_block(parent->children[index].kind)) {
        return std::nullopt;
    }

    RecordedBlockEdit result;
    bool replace = false;
    if (index + 1 < parent->children.size()) {
        if (auto next = first_editable_position(parent->children[index + 1])) result.target = *next;
        else replace = true;
    } else if (index > 0) {
        if (auto previous = last_editable_position(parent->children[index - 1])) result.target = *previous;
        else replace = true;
    } else {
        replace = true;
    }

    DocumentTreeEdit remove;
    remove.kind = DocumentTreeEditKind::Remove;
    remove.parent_id = parent->id;
    remove.index = index;
    remove.before = parent->children[index];
    result.operations.emplace_back(std::move(remove));
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));
    if (replace) {
        auto paragraph = empty_paragraph(allocator, document);
        result.target = {paragraph.id, 0, TextAffinity::Downstream};
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = parent->id;
        insert.index = index;
        insert.after = paragraph;
        result.operations.emplace_back(std::move(insert));
        if (!insert_block(*parent, index, std::move(paragraph))) return std::nullopt;
    }
    return result;
}

inline std::optional<DocumentTreeEdit> remove_node_recorded(
    EditorDocument& document,
    NodeId id) {
    auto path = block_path(document.root, id);
    if (!path || path->empty()) return std::nullopt;
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || index >= parent->children.size()) return std::nullopt;
    DocumentTreeEdit remove;
    remove.kind = DocumentTreeEditKind::Remove;
    remove.parent_id = parent->id;
    remove.index = index;
    remove.before = parent->children[index];
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));
    return remove;
}

inline bool prunable_empty_container(const BlockNode& block) {
    if (!block.children.empty()) return false;
    switch (block.kind) {
        case BlockKind::BlockQuote:
        case BlockKind::FootnoteDefinition:
        case BlockKind::List:
        case BlockKind::TaskList:
        case BlockKind::ListItem:
        case BlockKind::TaskListItem:
        case BlockKind::TableRow:
            return true;
        case BlockKind::Callout:
            return !block.callout_title.has_value();
        default:
            return false;
    }
}

inline void prune_empty_containers_recorded(
    BlockNode& parent,
    std::vector<DocumentOperation>& operations) {
    std::size_t index = 0;
    while (index < parent.children.size()) {
        prune_empty_containers_recorded(parent.children[index], operations);
        if (!prunable_empty_container(parent.children[index])) {
            ++index;
            continue;
        }
        DocumentTreeEdit remove;
        remove.kind = DocumentTreeEditKind::Remove;
        remove.parent_id = parent.id;
        remove.index = index;
        remove.before = parent.children[index];
        operations.emplace_back(std::move(remove));
        parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

enum class ListStyle { Bullet, Ordered, Task };

inline void validate_inline_document(NodeId owner, const InlineDocument& document, std::unordered_set<std::uint64_t>& ids, std::vector<DocumentInvariantError>& errors) {
    if (!tokens_partition_source(document.tree, document.source.size())) errors.push_back({owner, "inline tokens do not partition source"});
    if (!roots_partition_source(document.tree, document.source.size())) errors.push_back({owner, "inline roots do not partition source"});
    if (flatten_tokens(document.tree, document.source) != document.source) errors.push_back({owner, "inline CST is not lossless"});
    std::function<void(const InlineCstNodes&)> scan = [&](const InlineCstNodes& nodes) {
        for (const auto& node : nodes) {
            if (node.id.v == 0 || !ids.insert(node.id.v).second) errors.push_back({node.id, "duplicate or missing node id"});
            scan(node.children);
        }
    };
    scan(document.tree.nodes);
    for (const auto& token : document.tree.tokens) if (token.id.v == 0 || !ids.insert(token.id.v).second) errors.push_back({token.id, "duplicate or missing token id"});
}
inline void validate_blocks(const BlockVec& blocks, std::unordered_set<std::uint64_t>& ids, std::vector<DocumentInvariantError>& errors) {
    for (const auto& block : blocks) {
        if (block.id.v == 0 || !ids.insert(block.id.v).second) errors.push_back({block.id, "duplicate or missing block id"});
        if (const auto* document = editable_inline_document(block)) {
            validate_inline_document(block.id, *document, ids, errors);
        }
        if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
            if (!block_source_tokens_partition(block.block_source)) {
                errors.push_back({block.id, "block-source tokens do not partition source"});
            }
            if (flatten_block_source_tokens(block.block_source) != block.block_source.source) {
                errors.push_back({block.id, "block-source CST is not lossless"});
            }
        }
        validate_blocks(block.children, ids, errors);
    }
}

inline DocumentTransaction source_transaction(
    EditorDocument after,
    AppliedSourceEdit edit,
    TextSelection selection_before,
    TextSelection selection_after,
    std::uint64_t revision_before,
    DocumentTransactionReason reason) {
    std::vector<DocumentOperation> operations;
    operations.emplace_back(DocumentTextOperation{
        std::move(edit.forward),
        std::move(edit.inverse),
    });
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection_before,
        selection_after,
        revision_before,
        reason);
}

} // namespace document_edit_detail

} // namespace elmd

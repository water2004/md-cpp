export module elmd.core.document_edit;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.text_edit;
import elmd.core.utf;

export namespace elmd {

enum class DocumentTransactionReason { InsertText, Delete, Paste, Format, Structure };
enum class InlineFormat { Emphasis, Strong, Strikethrough, Code, Math };
enum class DocumentTableEdit {
    MoveCellNext, MoveCellPrevious,
    InsertRowAbove, InsertRowBelow, DeleteRow, MoveRowUp, MoveRowDown,
    InsertColumnLeft, InsertColumnRight, DeleteColumn, MoveColumnLeft, MoveColumnRight,
    SetColumnAlignment, Normalize, InsertRowAt, InsertColumnAt, MoveRowTo, MoveColumnTo,
};
enum class DocumentMove { Left, Right, Up, Down, LineStart, LineEnd, DocumentStart, DocumentEnd };

struct DocumentTransaction {
    EditorDocument before;
    EditorDocument after;
    TextSelection selection_before;
    TextSelection selection_after;
    DocumentTransactionReason reason = DocumentTransactionReason::Structure;
};

struct DocumentInvariantError { NodeId node_id{}; std::string message; };

class DocumentHistory {
public:
    explicit DocumentHistory(std::size_t capacity = 1000) : capacity_(capacity) {}
    void push(DocumentTransaction transaction) {
        undo_.push_back(std::move(transaction));
        redo_.clear();
        if (undo_.size() > capacity_) undo_.erase(undo_.begin());
    }
    std::optional<std::pair<EditorDocument, TextSelection>> undo() {
        if (undo_.empty()) return std::nullopt;
        auto transaction = std::move(undo_.back()); undo_.pop_back();
        auto result = std::pair{transaction.before, transaction.selection_before};
        redo_.push_back(std::move(transaction));
        return result;
    }
    std::optional<std::pair<EditorDocument, TextSelection>> redo() {
        if (redo_.empty()) return std::nullopt;
        auto transaction = std::move(redo_.back()); redo_.pop_back();
        auto result = std::pair{transaction.after, transaction.selection_after};
        undo_.push_back(std::move(transaction));
        return result;
    }
    void clear() { undo_.clear(); redo_.clear(); }
    bool has_undo() const { return !undo_.empty(); }
    bool has_redo() const { return !redo_.empty(); }
private:
    std::size_t capacity_;
    std::vector<DocumentTransaction> undo_;
    std::vector<DocumentTransaction> redo_;
};

namespace document_edit_detail {

inline bool text_block(BlockKind kind) { return kind == BlockKind::Paragraph || kind == BlockKind::Heading; }
inline bool atomic_block(BlockKind kind) {
    return kind == BlockKind::ImageBlock || kind == BlockKind::Toc || kind == BlockKind::Frontmatter
        || kind == BlockKind::ThematicBreak || kind == BlockKind::LinkDefinition
        || kind == BlockKind::UnsupportedMarkup || kind == BlockKind::Extension;
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
    if (text_block(block.kind) || block.kind == BlockKind::TableCell) scan_inline_ids(block.inline_content, maximum);
    if (block.callout_title) scan_inline_ids(*block.callout_title, maximum);
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
        if ((text_block(block.kind) || block.kind == BlockKind::TableCell) && block.id == id) return &block.inline_content;
        if (auto* found = find_inline_owner(block.children, id)) return found;
    }
    return nullptr;
}
inline const InlineDocument* find_inline_owner(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if ((text_block(block.kind) || block.kind == BlockKind::TableCell) && block.id == id) return &block.inline_content;
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
    if (text_block(block.kind) || block.kind == BlockKind::TableCell) assign_missing_ids(block.inline_content, owner, allocator);
    if (block.callout_title) assign_missing_ids(*block.callout_title, owner, allocator);
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

inline bool edit_inline(
    EditorDocument& document,
    NodeId owner_id,
    SourceRange range,
    std::u32string replacement,
    NodeAllocator& allocator) {
    auto* inline_document = find_inline_owner(document.root.children, owner_id);
    if (!inline_document || !range.valid_for(inline_document->source.size())) return false;
    apply_inline_source_edit(owner_id, *inline_document, TextEdit{owner_id, range, std::move(replacement)}, parse_context(document, allocator));
    return true;
}

struct InsertResult { std::size_t offset = 0; TextAffinity affinity = TextAffinity::Downstream; };
inline std::optional<InsertResult> insert_text(
    EditorDocument& document,
    TextPosition position,
    std::u32string_view text,
    NodeAllocator& allocator) {
    if (auto* inline_document = find_inline_owner(document.root.children, position.container_id)) {
        const auto offset = (std::min)(position.source_offset, inline_document->source.size());
        std::u32string replacement(text);
        std::size_t caret = offset + replacement.size();
        if (text.size() == 1 && (text.front() == U'*' || text.front() == U'_' || text.front() == U'~'
            || text.front() == U'`' || text.front() == U'$')) {
            replacement.push_back(text.front());
            caret = offset + 1;
        }
        if (!edit_inline(document, position.container_id, {offset, offset}, std::move(replacement), allocator)) return std::nullopt;
        return InsertResult{caret, TextAffinity::Downstream};
    }
    if (auto* block = elmd::find_block(document.root, position.container_id)) {
        auto* source = block->kind == BlockKind::CodeBlock ? &block->code_text
            : block->kind == BlockKind::MathBlock ? &block->tex : nullptr;
        if (!source) return std::nullopt;
        const auto offset = (std::min)(position.source_offset, source->size());
        source->insert(offset, text);
        return InsertResult{offset + text.size(), TextAffinity::Downstream};
    }
    return std::nullopt;
}

inline std::optional<std::size_t> editable_length(const EditorDocument& document, NodeId id) {
    if (const auto* inline_document = find_inline_owner(document.root.children, id)) return inline_document->source.size();
    if (const auto* block = elmd::find_block(document.root, id)) {
        if (block->kind == BlockKind::CodeBlock) return block->code_text.size();
        if (block->kind == BlockKind::MathBlock) return block->tex.size();
        if (atomic_block(block->kind)) return 1;
    }
    return std::nullopt;
}

inline bool erase_text(EditorDocument& document, NodeId id, SourceRange range, NodeAllocator& allocator) {
    if (find_inline_owner(document.root.children, id)) return edit_inline(document, id, range, {}, allocator);
    auto* block = elmd::find_block(document.root, id);
    if (!block) return false;
    auto* source = block->kind == BlockKind::CodeBlock ? &block->code_text
        : block->kind == BlockKind::MathBlock ? &block->tex : nullptr;
    if (!source || !range.valid_for(source->size())) return false;
    source->erase(range.start, range.length());
    return true;
}

inline bool split_direct(
    BlockVec& blocks,
    NodeId id,
    std::size_t offset,
    EditorDocument& document,
    NodeAllocator& allocator,
    TextPosition& target) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto& block = blocks[index];
        if (block.id == id && text_block(block.kind)) {
            offset = (std::min)(offset, block.inline_content.source.size());
            auto right_source = block.inline_content.source.substr(offset);
            block.inline_content.source.erase(offset);
            reparse(block.inline_content, document, allocator);
            BlockNode right;
            right.id = allocator.allocate();
            right.kind = BlockKind::Paragraph;
            right.inline_content = make_inline(std::move(right_source), document, allocator);
            blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(right));
            target = TextPosition{blocks[index + 1].id, 0, TextAffinity::Downstream};
            return true;
        }
        if (split_direct(block.children, id, offset, document, allocator, target)) return true;
    }
    return false;
}

inline bool join_adjacent(
    BlockVec& blocks,
    NodeId id,
    bool backward,
    EditorDocument& document,
    NodeAllocator& allocator,
    TextPosition& target) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].id == id && text_block(blocks[index].kind)) {
            if (backward && index > 0 && text_block(blocks[index - 1].kind)) {
                const auto offset = blocks[index - 1].inline_content.source.size();
                blocks[index - 1].inline_content.source += blocks[index].inline_content.source;
                reparse(blocks[index - 1].inline_content, document, allocator);
                const auto owner = blocks[index - 1].id;
                blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
                target = TextPosition{owner, offset, TextAffinity::Upstream};
                return true;
            }
            if (!backward && index + 1 < blocks.size() && text_block(blocks[index + 1].kind)) {
                const auto offset = blocks[index].inline_content.source.size();
                blocks[index].inline_content.source += blocks[index + 1].inline_content.source;
                reparse(blocks[index].inline_content, document, allocator);
                blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index + 1));
                target = TextPosition{blocks[index].id, offset, TextAffinity::Downstream};
                return true;
            }
        }
        if (join_adjacent(blocks[index].children, id, backward, document, allocator, target)) return true;
    }
    return false;
}

inline bool remove_atomic(BlockVec& blocks, NodeId id, NodeAllocator& allocator, const EditorDocument& owner, TextPosition& target) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].id == id && atomic_block(blocks[index].kind)) {
            if (index + 1 < blocks.size()) target = {blocks[index + 1].id, 0, TextAffinity::Downstream};
            else if (index > 0) {
                const auto& previous = blocks[index - 1];
                const auto length = text_block(previous.kind) ? previous.inline_content.source.size()
                    : previous.kind == BlockKind::CodeBlock ? previous.code_text.size()
                    : previous.kind == BlockKind::MathBlock ? previous.tex.size() : std::size_t{1};
                target = {previous.id, length, TextAffinity::Upstream};
            }
            else {
                blocks[index] = empty_paragraph(allocator, owner);
                target = {blocks[index].id, 0, TextAffinity::Downstream};
                return true;
            }
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }
        if (remove_atomic(blocks[index].children, id, allocator, owner, target)) return true;
    }
    return false;
}

inline bool remove_node(BlockVec& blocks, NodeId id) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].id == id) {
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }
        if (remove_node(blocks[index].children, id)) return true;
    }
    return false;
}

inline void prune_empty_containers(BlockVec& blocks) {
    for (auto& block : blocks) prune_empty_containers(block.children);
    std::erase_if(blocks, [](const BlockNode& block) {
        switch (block.kind) {
            case BlockKind::BlockQuote:
            case BlockKind::Callout:
            case BlockKind::FootnoteDefinition:
            case BlockKind::List:
            case BlockKind::TaskList:
            case BlockKind::ListItem:
            case BlockKind::TaskListItem:
            case BlockKind::TableRow:
                return block.children.empty();
            default:
                return false;
        }
    });
}

struct EditableNode { NodeId id{}; std::u32string text; };
inline void collect_editable_nodes(const BlockVec& blocks, std::vector<EditableNode>& output) {
    for (const auto& block : blocks) {
        if (text_block(block.kind)) output.push_back({block.id, block.inline_content.source});
        else if (block.kind == BlockKind::CodeBlock) output.push_back({block.id, block.code_text});
        else if (block.kind == BlockKind::MathBlock) output.push_back({block.id, block.tex});
        else if (atomic_block(block.kind)) output.push_back({block.id, U"\ufffc"});
        else if (block.kind == BlockKind::TableCell) output.push_back({block.id, block.inline_content.source});
        collect_editable_nodes(block.children, output);
    }
}

inline bool set_heading(BlockVec& blocks, NodeId id, std::uint8_t level) {
    for (auto& block : blocks) {
        if (block.id == id && text_block(block.kind)) {
            block.kind = level == 0 ? BlockKind::Paragraph : BlockKind::Heading;
            block.level = level;
            return true;
        }
        if (set_heading(block.children, id, level)) return true;
    }
    return false;
}

inline std::optional<std::pair<std::size_t, std::size_t>> direct_range(const BlockVec& blocks, NodeId first, NodeId last) {
    std::optional<std::size_t> begin, end;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].id == first) begin = index;
        if (blocks[index].id == last) end = index;
    }
    if (!begin || !end) return std::nullopt;
    if (*begin > *end) std::swap(*begin, *end);
    return std::pair{*begin, *end};
}

inline bool toggle_quote(BlockVec& blocks, NodeId first, NodeId last, NodeAllocator& allocator) {
    if (auto range = direct_range(blocks, first, last)) {
        if (range->first == range->second && blocks[range->first].kind == BlockKind::BlockQuote) {
            auto children = std::move(blocks[range->first].children);
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(range->first));
            blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(range->first),
                std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
            return true;
        }
        BlockNode quote; quote.id = allocator.allocate(); quote.kind = BlockKind::BlockQuote;
        quote.children.insert(quote.children.end(),
            std::make_move_iterator(blocks.begin() + static_cast<std::ptrdiff_t>(range->first)),
            std::make_move_iterator(blocks.begin() + static_cast<std::ptrdiff_t>(range->second + 1)));
        blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(range->first), blocks.begin() + static_cast<std::ptrdiff_t>(range->second + 1));
        blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(range->first), std::move(quote));
        return true;
    }
    for (auto& block : blocks) {
        if (toggle_quote(block.children, first, last, allocator)) return true;
    }
    return false;
}

inline bool toggle_callout(BlockVec& blocks, NodeId first, NodeId last, std::string kind, NodeAllocator& allocator) {
    if (auto range = direct_range(blocks, first, last)) {
        if (range->first == range->second && blocks[range->first].kind == BlockKind::Callout) {
            auto children = std::move(blocks[range->first].children);
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(range->first));
            blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(range->first),
                std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
            return true;
        }
        BlockNode callout; callout.id = allocator.allocate(); callout.kind = BlockKind::Callout; callout.callout_kind = std::move(kind);
        callout.children.insert(callout.children.end(),
            std::make_move_iterator(blocks.begin() + static_cast<std::ptrdiff_t>(range->first)),
            std::make_move_iterator(blocks.begin() + static_cast<std::ptrdiff_t>(range->second + 1)));
        blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(range->first), blocks.begin() + static_cast<std::ptrdiff_t>(range->second + 1));
        blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(range->first), std::move(callout));
        return true;
    }
    for (auto& block : blocks) {
        if (toggle_callout(block.children, first, last, kind, allocator)) return true;
    }
    return false;
}

enum class ListStyle { Bullet, Ordered, Task };
inline bool toggle_list(BlockVec& blocks, NodeId first, NodeId last, ListStyle style, NodeAllocator& allocator) {
    if (auto range = direct_range(blocks, first, last)) {
        if (range->first == range->second && (blocks[range->first].kind == BlockKind::List || blocks[range->first].kind == BlockKind::TaskList)) {
            auto list = std::move(blocks[range->first]);
            BlockVec unwrapped;
            for (auto& item : list.children) for (auto& child : item.children) unwrapped.push_back(std::move(child));
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(range->first));
            blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(range->first),
                std::make_move_iterator(unwrapped.begin()), std::make_move_iterator(unwrapped.end()));
            return true;
        }
        BlockNode list; list.id = allocator.allocate(); list.kind = style == ListStyle::Task ? BlockKind::TaskList : BlockKind::List;
        list.list_ordered = style == ListStyle::Ordered;
        for (std::size_t index = range->first; index <= range->second; ++index) {
            BlockNode item;
            item.id = allocator.allocate();
            item.kind = style == ListStyle::Task ? BlockKind::TaskListItem : BlockKind::ListItem;
            item.marker = style == ListStyle::Task ? U"- [ ] " : style == ListStyle::Ordered ? U"1. " : U"- ";
            item.children.push_back(std::move(blocks[index]));
            list.children.push_back(std::move(item));
        }
        blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(range->first), blocks.begin() + static_cast<std::ptrdiff_t>(range->second + 1));
        blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(range->first), std::move(list));
        return true;
    }
    for (auto& block : blocks) {
        if (toggle_list(block.children, first, last, style, allocator)) return true;
    }
    return false;
}

inline bool toggle_task(BlockVec& blocks, NodeId id) {
    for (auto& block : blocks) {
        if (block.kind == BlockKind::TaskListItem && elmd::find_block(block, id)) {
            block.checked = !block.checked;
            return true;
        }
        if (toggle_task(block.children, id)) return true;
    }
    return false;
}

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
        if (text_block(block.kind) || block.kind == BlockKind::TableCell) validate_inline_document(block.id, block.inline_content, ids, errors);
        validate_blocks(block.children, ids, errors);
    }
}

inline DocumentTransaction transaction(EditorDocument before, EditorDocument after, TextSelection selection_before, TextSelection selection_after, DocumentTransactionReason reason) {
    return DocumentTransaction{std::move(before), std::move(after), selection_before, selection_after, reason};
}

} // namespace document_edit_detail

inline void normalize_document(EditorDocument& document) {
    document_edit_detail::NodeAllocator allocator(document);
    if (document.root.id.v == 0) document.root.id = allocator.allocate();
    document.root.kind = BlockKind::Document;
    if (document.root.children.empty()) document.root.children.push_back(document_edit_detail::empty_paragraph(allocator, document));
    document_edit_detail::normalize_blocks(document.root.children, document, allocator);
}

inline std::vector<DocumentInvariantError> validate_document(const EditorDocument& document) {
    std::vector<DocumentInvariantError> errors;
    std::unordered_set<std::uint64_t> ids;
    if (document.root.id.v == 0) errors.push_back({document.root.id, "document root has no id"});
    else ids.insert(document.root.id.v);
    if (document.root.children.empty()) errors.push_back({{}, "document has no blocks"});
    document_edit_detail::validate_blocks(document.root.children, ids, errors);
    return errors;
}

inline std::optional<DocumentTransaction> document_delete_selection(const EditorDocument&, const TextSelection&);

inline std::optional<DocumentTransaction> document_insert_text(const EditorDocument& document, const TextSelection& selection, std::u32string_view text) {
    if (text.empty()) return std::nullopt;
    auto working = document;
    auto current = selection;
    if (!selection.is_caret()) {
        auto deletion = document_delete_selection(document, selection);
        if (!deletion) return std::nullopt;
        working = std::move(deletion->after);
        current = deletion->selection_after;
    }
    document_edit_detail::NodeAllocator allocator(working);
    auto inserted = document_edit_detail::insert_text(working, current.active, text, allocator);
    if (!inserted) return std::nullopt;
    ++working.revision;
    auto target = current.active; target.source_offset = inserted->offset; target.affinity = inserted->affinity;
    return document_edit_detail::transaction(document, std::move(working), selection, TextSelection::caret(target), DocumentTransactionReason::InsertText);
}

inline std::optional<DocumentTransaction> document_enter(const EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    auto after = document;
    document_edit_detail::NodeAllocator allocator(after);
    TextPosition target;
    if (!document_edit_detail::split_direct(after.root.children, selection.active.container_id, selection.active.source_offset, after, allocator, target)) return std::nullopt;
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_paste_text(const EditorDocument& document, const TextSelection& selection, std::u32string_view text) {
    if (text.empty()) return std::nullopt;
    std::u32string normalized;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == U'\r') { if (index + 1 < text.size() && text[index + 1] == U'\n') ++index; normalized.push_back(U'\n'); }
        else normalized.push_back(text[index]);
    }
    auto working = document;
    auto current = selection;
    if (!current.is_caret()) {
        auto deletion = document_delete_selection(working, current); if (!deletion) return std::nullopt;
        working = std::move(deletion->after); current = deletion->selection_after;
    }
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const auto end = normalized.find(U'\n', start);
        const auto segment_end = end == std::u32string::npos ? normalized.size() : end;
        if (segment_end > start) {
            auto insertion = document_insert_text(working, current, std::u32string_view(normalized).substr(start, segment_end - start));
            if (!insertion) return std::nullopt; working = std::move(insertion->after); current = insertion->selection_after;
        }
        if (end == std::u32string::npos) break;
        auto split = document_enter(working, current); if (!split) return std::nullopt;
        working = std::move(split->after); current = split->selection_after; start = end + 1;
    }
    return document_edit_detail::transaction(document, std::move(working), selection, current, DocumentTransactionReason::Paste);
}

inline std::u32string format_marker(InlineFormat format) {
    switch (format) {
        case InlineFormat::Emphasis: return U"*";
        case InlineFormat::Strong: return U"**";
        case InlineFormat::Strikethrough: return U"~~";
        case InlineFormat::Code: return U"`";
        case InlineFormat::Math: return U"$";
    }
    return {};
}

inline std::optional<DocumentTransaction> document_toggle_inline_format(const EditorDocument& document, const TextSelection& selection, InlineFormat format) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document;
    document_edit_detail::NodeAllocator allocator(after);
    auto* inline_document = document_edit_detail::find_inline_owner(after.root.children, selection.active.container_id);
    if (!inline_document) return std::nullopt;
    auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    auto end = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
    start = (std::min)(start, inline_document->source.size()); end = (std::min)(end, inline_document->source.size());
    const auto marker = format_marker(format);
    SourceRange edit_range{start, end};
    std::u32string replacement;
    TextSelection result = selection;
    if (start >= marker.size() && end + marker.size() <= inline_document->source.size()
        && inline_document->source.substr(start - marker.size(), marker.size()) == marker
        && inline_document->source.substr(end, marker.size()) == marker) {
        edit_range = {start - marker.size(), end + marker.size()};
        replacement = inline_document->source.substr(start, end - start);
        result.anchor.source_offset -= marker.size(); result.active.source_offset -= marker.size();
    } else {
        replacement = marker + inline_document->source.substr(start, end - start) + marker;
        result.anchor.source_offset += marker.size(); result.active.source_offset += marker.size();
        if (selection.is_caret()) result = TextSelection::caret(TextPosition{selection.active.container_id, start + marker.size(), TextAffinity::Downstream});
    }
    if (!document_edit_detail::edit_inline(after, selection.active.container_id, edit_range, std::move(replacement), allocator)) return std::nullopt;
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, result, DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_link(const EditorDocument& document, const TextSelection& selection, std::string href, std::optional<std::string> title) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    auto* owner = document_edit_detail::find_inline_owner(after.root.children, selection.active.container_id); if (!owner) return std::nullopt;
    const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    const auto end = (std::min)((std::max)(selection.anchor.source_offset, selection.active.source_offset), owner->source.size());
    const auto label = owner->source.substr(start, end - start);
    std::u32string replacement = U"[" + label + U"](" + utf8_to_cps(href);
    if (title) replacement += U" \"" + utf8_to_cps(*title) + U"\"";
    replacement += U")";
    if (!document_edit_detail::edit_inline(after, selection.active.container_id, {start, end}, std::move(replacement), allocator)) return std::nullopt;
    ++after.revision;
    const auto target = TextPosition{selection.active.container_id, start + 1 + label.size(), TextAffinity::Downstream};
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_image(const EditorDocument& document, const TextSelection& selection, std::string path, std::string alt) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    auto* owner = document_edit_detail::find_inline_owner(after.root.children, selection.active.container_id); if (!owner) return std::nullopt;
    const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    const auto end = (std::min)((std::max)(selection.anchor.source_offset, selection.active.source_offset), owner->source.size());
    auto label = alt.empty() ? owner->source.substr(start, end - start) : utf8_to_cps(alt);
    const auto replacement = U"![" + label + U"](" + utf8_to_cps(path.empty() ? "image.png" : path) + U")";
    if (!document_edit_detail::edit_inline(after, selection.active.container_id, {start, end}, replacement, allocator)) return std::nullopt;
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection,
        TextSelection::caret({selection.active.container_id, start + 2 + label.size(), TextAffinity::Downstream}), DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_soft_break(const EditorDocument& document, const TextSelection& selection) {
    return document_insert_text(document, selection, U"\n");
}

inline std::optional<DocumentTransaction> document_delete_backward(const EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return document_delete_selection(document, selection);
    auto after = document; auto target = selection.active; document_edit_detail::NodeAllocator allocator(after);
    if (target.source_offset > 0) {
        const std::size_t start = target.source_offset - 1, end = target.source_offset;
        if (!document_edit_detail::erase_text(after, target.container_id, {start, end}, allocator)) return std::nullopt;
        target.source_offset = start;
    } else if (!document_edit_detail::join_adjacent(after.root.children, target.container_id, true, after, allocator, target)) {
        if (!document_edit_detail::remove_atomic(after.root.children, target.container_id, allocator, after, target)) return std::nullopt;
    }
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Delete);
}

inline std::optional<DocumentTransaction> document_delete_forward(const EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return document_delete_selection(document, selection);
    auto after = document; auto target = selection.active; document_edit_detail::NodeAllocator allocator(after);
    const auto length = document_edit_detail::editable_length(after, target.container_id); if (!length) return std::nullopt;
    if (target.source_offset < *length) {
        if (!document_edit_detail::erase_text(after, target.container_id, {target.source_offset, target.source_offset + 1}, allocator)) return std::nullopt;
    } else if (!document_edit_detail::join_adjacent(after.root.children, target.container_id, false, after, allocator, target)) return std::nullopt;
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Delete);
}

inline std::optional<DocumentTransaction> document_delete_selection(const EditorDocument& document, const TextSelection& selection) {
    if (selection.is_caret()) return std::nullopt;
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (selection.anchor.container_id == selection.active.container_id) {
        const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
        const auto end = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
        if (!document_edit_detail::erase_text(after, selection.active.container_id, {start, end}, allocator)) return std::nullopt;
        ++after.revision;
        const auto target = TextPosition{selection.active.container_id, start, TextAffinity::Downstream};
        return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Delete);
    }

    std::vector<document_edit_detail::EditableNode> nodes;
    document_edit_detail::collect_editable_nodes(after.root.children, nodes);
    auto index_of = [&](NodeId id) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < nodes.size(); ++index) if (nodes[index].id == id) return index;
        return std::nullopt;
    };
    auto anchor_index = index_of(selection.anchor.container_id);
    auto active_index = index_of(selection.active.container_id);
    if (!anchor_index || !active_index) return std::nullopt;
    auto first = selection.anchor;
    auto last = selection.active;
    if (*anchor_index > *active_index) {
        std::swap(anchor_index, active_index);
        std::swap(first, last);
    }
    auto* first_owner = document_edit_detail::find_inline_owner(after.root.children, first.container_id);
    auto* last_owner = document_edit_detail::find_inline_owner(after.root.children, last.container_id);
    if (!first_owner || !last_owner) return std::nullopt;
    first.source_offset = (std::min)(first.source_offset, first_owner->source.size());
    last.source_offset = (std::min)(last.source_offset, last_owner->source.size());
    auto replacement = first_owner->source.substr(0, first.source_offset)
        + last_owner->source.substr(last.source_offset);
    if (!document_edit_detail::edit_inline(after, first.container_id, {0, first_owner->source.size()}, std::move(replacement), allocator)) return std::nullopt;
    for (std::size_t index = *anchor_index + 1; index <= *active_index; ++index) {
        document_edit_detail::remove_node(after.root.children, nodes[index].id);
    }
    document_edit_detail::prune_empty_containers(after.root.children);
    if (after.root.children.empty()) after.root.children.push_back(document_edit_detail::empty_paragraph(allocator, after));
    ++after.revision;
    const auto target = TextPosition{first.container_id, first.source_offset, TextAffinity::Downstream};
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Delete);
}

inline std::optional<TextSelection> document_move_selection(const EditorDocument& document, const TextSelection& selection, DocumentMove movement, bool extend) {
    std::vector<document_edit_detail::EditableNode> nodes; document_edit_detail::collect_editable_nodes(document.root.children, nodes);
    if (nodes.empty()) return std::nullopt;
    auto index_of = [&](NodeId id) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < nodes.size(); ++index) if (nodes[index].id == id) return index;
        return std::nullopt;
    };
    auto index = index_of(selection.active.container_id); if (!index) return std::nullopt;
    auto target = selection.active; target.source_offset = (std::min)(target.source_offset, nodes[*index].text.size());
    if (!extend && !selection.is_caret()) {
        const auto anchor_index = index_of(selection.anchor.container_id).value_or(*index);
        const bool anchor_first = anchor_index < *index || (anchor_index == *index && selection.anchor.source_offset < selection.active.source_offset);
        target = (movement == DocumentMove::Left || movement == DocumentMove::Up || movement == DocumentMove::LineStart || movement == DocumentMove::DocumentStart)
            ? (anchor_first ? selection.anchor : selection.active) : (anchor_first ? selection.active : selection.anchor);
        return TextSelection::caret(target);
    }
    if (movement == DocumentMove::DocumentStart) target = {nodes.front().id, 0, TextAffinity::Downstream};
    else if (movement == DocumentMove::DocumentEnd) target = {nodes.back().id, nodes.back().text.size(), TextAffinity::Downstream};
    else if (movement == DocumentMove::Left) {
        if (target.source_offset > 0) --target.source_offset;
        else if (*index > 0) target = {nodes[*index - 1].id, nodes[*index - 1].text.size(), TextAffinity::Upstream};
    } else if (movement == DocumentMove::Right) {
        if (target.source_offset < nodes[*index].text.size()) ++target.source_offset;
        else if (*index + 1 < nodes.size()) target = {nodes[*index + 1].id, 0, TextAffinity::Downstream};
    } else {
        const auto& text = nodes[*index].text;
        const auto before = target.source_offset == 0 ? std::u32string::npos : text.rfind(U'\n', target.source_offset - 1);
        const auto line_start = before == std::u32string::npos ? 0 : before + 1;
        const auto after = text.find(U'\n', target.source_offset);
        const auto line_end = after == std::u32string::npos ? text.size() : after;
        if (movement == DocumentMove::LineStart) target.source_offset = line_start;
        else if (movement == DocumentMove::LineEnd) target.source_offset = line_end;
        else {
            const auto column = target.source_offset - line_start;
            if (movement == DocumentMove::Up && line_start > 0) {
                const auto previous_end = line_start - 1;
                const auto previous_break = previous_end == 0 ? std::u32string::npos : text.rfind(U'\n', previous_end - 1);
                const auto previous_start = previous_break == std::u32string::npos ? 0 : previous_break + 1;
                target.source_offset = previous_start + (std::min)(column, previous_end - previous_start);
            } else if (movement == DocumentMove::Down && line_end < text.size()) {
                const auto next_start = line_end + 1;
                const auto next_break = text.find(U'\n', next_start);
                const auto next_end = next_break == std::u32string::npos ? text.size() : next_break;
                target.source_offset = next_start + (std::min)(column, next_end - next_start);
            }
        }
    }
    if (target == selection.active) return std::nullopt;
    return extend ? TextSelection{selection.anchor, target} : TextSelection::caret(target);
}

inline std::optional<TextSelection> document_select_all(const EditorDocument& document) {
    std::vector<document_edit_detail::EditableNode> nodes; document_edit_detail::collect_editable_nodes(document.root.children, nodes);
    if (nodes.empty()) return std::nullopt;
    return TextSelection{{nodes.front().id, 0, TextAffinity::Downstream}, {nodes.back().id, nodes.back().text.size(), TextAffinity::Downstream}};
}

inline std::optional<DocumentTransaction> document_set_heading(const EditorDocument& document, const TextSelection& selection, std::uint8_t level) {
    if (level > 6 || selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document; if (!document_edit_detail::set_heading(after.root.children, selection.active.container_id, level)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_block_quote(const EditorDocument& document, const TextSelection& selection) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::toggle_quote(after.root.children, selection.anchor.container_id, selection.active.container_id, allocator)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_callout(const EditorDocument& document, const TextSelection& selection, std::string kind) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::toggle_callout(after.root.children, selection.anchor.container_id, selection.active.container_id, std::move(kind), allocator)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_list(const EditorDocument& document, const TextSelection& selection, document_edit_detail::ListStyle style) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::toggle_list(after.root.children, selection.anchor.container_id, selection.active.container_id, style, allocator)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}
using ListStyle = document_edit_detail::ListStyle;

inline std::optional<DocumentTransaction> document_toggle_task_checkbox(const EditorDocument& document, const TextSelection& selection) {
    auto after = document; if (!document_edit_detail::toggle_task(after.root.children, selection.active.container_id)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline BlockNode make_code_block(std::optional<std::string> language = std::nullopt) { BlockNode block; block.kind = BlockKind::CodeBlock; block.language = std::move(language); return block; }
inline BlockNode make_math_block() { BlockNode block; block.kind = BlockKind::MathBlock; block.math_delim = MathDelimiter::BlockDollar; return block; }
inline BlockNode make_toc_block() { BlockNode block; block.kind = BlockKind::Toc; return block; }

inline BlockNode make_table_block(const EditorDocument& document, std::size_t rows, std::size_t columns) {
    auto working = document; document_edit_detail::NodeAllocator allocator(working);
    BlockNode table; table.kind = BlockKind::Table; columns = (std::max)(columns, std::size_t{1}); table.table_aligns.assign(columns, TableAlignment::None);
    BlockNode header; header.id = allocator.allocate(); header.kind = BlockKind::TableRow; header.table_header_row = true;
    for (std::size_t column = 0; column < columns; ++column) {
        BlockNode cell; cell.id = allocator.allocate(); cell.kind = BlockKind::TableCell;
        cell.inline_content = document_edit_detail::make_inline(U"Header", working, allocator);
        header.children.push_back(std::move(cell));
    }
    table.children.push_back(std::move(header));
    for (std::size_t row_index = 0; row_index < rows; ++row_index) {
        BlockNode row; row.id = allocator.allocate(); row.kind = BlockKind::TableRow;
        for (std::size_t column = 0; column < columns; ++column) {
            BlockNode cell; cell.id = allocator.allocate(); cell.kind = BlockKind::TableCell;
            cell.inline_content = document_edit_detail::make_inline(U"Cell", working, allocator);
            row.children.push_back(std::move(cell));
        }
        table.children.push_back(std::move(row));
    }
    return table;
}

inline std::optional<DocumentTransaction> document_insert_atomic_block(const EditorDocument& document, const TextSelection& selection, BlockNode block) {
    if (!selection.is_caret()) return std::nullopt;
    auto after = document; document_edit_detail::NodeAllocator allocator(after); document_edit_detail::assign_missing_ids(block, after, allocator);
    for (std::size_t index = 0; index < after.root.children.size(); ++index) {
        if (after.root.children[index].id != selection.active.container_id) continue;
        const auto inserted_id = block.id;
        after.root.children.insert(after.root.children.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(block));
        ++after.revision;
        return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret({inserted_id, 0, TextAffinity::Downstream}), DocumentTransactionReason::Structure);
    }
    return std::nullopt;
}

inline std::optional<DocumentTransaction> document_insert_footnote(const EditorDocument& document, const TextSelection& selection, std::string label) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    BlockNode footnote; footnote.id = allocator.allocate(); footnote.kind = BlockKind::FootnoteDefinition; footnote.footnote_label = std::move(label); footnote.children.push_back(document_edit_detail::empty_paragraph(allocator, after));
    after.root.children.push_back(std::move(footnote)); ++after.revision;
    const auto target = TextPosition{after.root.children.back().children.front().id, 0, TextAffinity::Downstream};
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_indent_list_item(const EditorDocument& document, const TextSelection& selection) {
    auto after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path) return std::nullopt;
    while (!path->empty()) {
        const auto* node = block_at_path(after.root, *path);
        if (node && (node->kind == BlockKind::ListItem || node->kind == BlockKind::TaskListItem)) break;
        path->pop_back();
    }
    if (path->empty()) return std::nullopt;
    const auto item_id = block_at_path(after.root, *path)->id;
    auto list_path = *path;
    const auto item_index = list_path.back();
    list_path.pop_back();
    auto* list = block_at_path(after.root, list_path);
    if (!list || (list->kind != BlockKind::List && list->kind != BlockKind::TaskList) || item_index == 0) return std::nullopt;
    document_edit_detail::NodeAllocator allocator(after);
    auto item = remove_block(*list, item_index);
    if (!item) return std::nullopt;
    auto& previous = list->children[item_index - 1];
    BlockNode* nested = nullptr;
    if (!previous.children.empty() && previous.children.back().kind == list->kind) nested = &previous.children.back();
    if (!nested) {
        BlockNode created;
        created.id = allocator.allocate();
        created.kind = list->kind;
        created.list_ordered = list->list_ordered;
        created.list_start = list->list_start;
        created.list_delimiter = list->list_delimiter;
        previous.children.push_back(std::move(created));
        nested = &previous.children.back();
    }
    nested->children.push_back(std::move(*item));
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_outdent_list_item(const EditorDocument& document, const TextSelection& selection) {
    auto after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path) return std::nullopt;
    while (!path->empty()) {
        const auto* node = block_at_path(after.root, *path);
        if (node && (node->kind == BlockKind::ListItem || node->kind == BlockKind::TaskListItem)) break;
        path->pop_back();
    }
    if (path->empty()) return std::nullopt;
    auto list_path = *path;
    const auto item_index = list_path.back();
    list_path.pop_back();
    auto* list = block_at_path(after.root, list_path);
    if (!list || (list->kind != BlockKind::List && list->kind != BlockKind::TaskList)) return std::nullopt;
    const auto list_id = list->id;
    auto item = remove_block(*list, item_index);
    if (!item) return std::nullopt;

    auto parent_item_path = list_path;
    if (!parent_item_path.empty()) parent_item_path.pop_back();
    auto* parent_item = block_at_path(after.root, parent_item_path);
    if (parent_item && (parent_item->kind == BlockKind::ListItem || parent_item->kind == BlockKind::TaskListItem)) {
        auto grand_list_path = parent_item_path;
        const auto parent_item_index = grand_list_path.back();
        grand_list_path.pop_back();
        auto* grand_list = block_at_path(after.root, grand_list_path);
        if (!grand_list) return std::nullopt;
        insert_block(*grand_list, parent_item_index + 1, std::move(*item));
        if (auto* nested = elmd::find_block(after.root, list_id); nested && nested->children.empty()) {
            auto* owner = find_parent_block(after.root, list_id);
            if (owner) {
                const auto nested_path = block_path(after.root, list_id);
                if (nested_path) remove_block(*owner, nested_path->back());
            }
        }
    } else {
        auto* list_parent = find_parent_block(after.root, list_id);
        auto list_location = block_path(after.root, list_id);
        if (!list_parent || !list_location) return std::nullopt;
        auto insertion = list_location->back() + 1;
        for (auto& child : item->children) insert_block(*list_parent, insertion++, std::move(child));
        if (auto* current_list = elmd::find_block(after.root, list_id); current_list && current_list->children.empty()) {
            remove_block(*list_parent, list_location->back());
        }
    }
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_edit_table(
    const EditorDocument& document,
    const TextSelection& selection,
    DocumentTableEdit edit,
    TableAlignment alignment = TableAlignment::None,
    std::size_t argument = 0) {
    auto after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path || path->size() < 2) return std::nullopt;
    while (!path->empty() && block_at_path(after.root, *path)->kind != BlockKind::TableCell) path->pop_back();
    if (path->size() < 2) return std::nullopt;
    const auto cell_id = block_at_path(after.root, *path)->id;
    auto row_path = *path;
    const auto column = row_path.back();
    row_path.pop_back();
    auto table_path = row_path;
    const auto row_index = table_path.back();
    table_path.pop_back();
    auto* table = block_at_path(after.root, table_path);
    if (!table || table->kind != BlockKind::Table || table->children.empty()) return std::nullopt;
    document_edit_detail::NodeAllocator allocator(after);
    const auto column_count = table->children.front().children.size();
    auto make_cell = [&] {
        BlockNode cell;
        cell.id = allocator.allocate();
        cell.kind = BlockKind::TableCell;
        cell.inline_content = document_edit_detail::make_inline({}, after, allocator);
        return cell;
    };
    auto make_row = [&] {
        BlockNode row;
        row.id = allocator.allocate();
        row.kind = BlockKind::TableRow;
        for (std::size_t index = 0; index < column_count; ++index) row.children.push_back(make_cell());
        return row;
    };
    auto target_id = cell_id;
    bool changed = true;
    switch (edit) {
        case DocumentTableEdit::MoveCellNext: {
            if (column + 1 < table->children[row_index].children.size()) target_id = table->children[row_index].children[column + 1].id;
            else if (row_index + 1 < table->children.size()) target_id = table->children[row_index + 1].children.front().id;
            else return std::nullopt;
            changed = false;
            break;
        }
        case DocumentTableEdit::MoveCellPrevious: {
            if (column > 0) target_id = table->children[row_index].children[column - 1].id;
            else if (row_index > 0) target_id = table->children[row_index - 1].children.back().id;
            else return std::nullopt;
            changed = false;
            break;
        }
        case DocumentTableEdit::InsertRowAbove:
            table->children.insert(table->children.begin() + static_cast<std::ptrdiff_t>(row_index), make_row());
            break;
        case DocumentTableEdit::InsertRowBelow:
            table->children.insert(table->children.begin() + static_cast<std::ptrdiff_t>(row_index + 1), make_row());
            break;
        case DocumentTableEdit::InsertRowAt: {
            const auto index = (std::min)(argument, table->children.size());
            table->children.insert(table->children.begin() + static_cast<std::ptrdiff_t>(index), make_row());
            break;
        }
        case DocumentTableEdit::DeleteRow:
            if (row_index == 0 || table->children.size() <= 1) return std::nullopt;
            table->children.erase(table->children.begin() + static_cast<std::ptrdiff_t>(row_index));
            target_id = table->children[(std::min)(row_index - 1, table->children.size() - 1)].children[(std::min)(column, column_count - 1)].id;
            break;
        case DocumentTableEdit::MoveRowUp:
            if (row_index <= 1) return std::nullopt;
            std::swap(table->children[row_index], table->children[row_index - 1]);
            break;
        case DocumentTableEdit::MoveRowDown:
            if (row_index == 0 || row_index + 1 >= table->children.size()) return std::nullopt;
            std::swap(table->children[row_index], table->children[row_index + 1]);
            break;
        case DocumentTableEdit::MoveRowTo: {
            if (row_index == 0 || argument == 0 || argument >= table->children.size()) return std::nullopt;
            auto row = remove_block(*table, row_index);
            if (!row) return std::nullopt;
            insert_block(*table, argument, std::move(*row));
            break;
        }
        case DocumentTableEdit::InsertColumnLeft:
        case DocumentTableEdit::InsertColumnRight:
        case DocumentTableEdit::InsertColumnAt: {
            const auto index = edit == DocumentTableEdit::InsertColumnLeft ? column
                : edit == DocumentTableEdit::InsertColumnRight ? column + 1
                : (std::min)(argument, column_count);
            for (auto& row : table->children) row.children.insert(row.children.begin() + static_cast<std::ptrdiff_t>(index), make_cell());
            table->table_aligns.insert(table->table_aligns.begin() + static_cast<std::ptrdiff_t>((std::min)(index, table->table_aligns.size())), TableAlignment::None);
            break;
        }
        case DocumentTableEdit::DeleteColumn:
            if (column_count <= 1) return std::nullopt;
            for (auto& row : table->children) row.children.erase(row.children.begin() + static_cast<std::ptrdiff_t>(column));
            if (column < table->table_aligns.size()) table->table_aligns.erase(table->table_aligns.begin() + static_cast<std::ptrdiff_t>(column));
            target_id = table->children[row_index].children[(std::min)(column, column_count - 2)].id;
            break;
        case DocumentTableEdit::MoveColumnLeft:
            if (column == 0) return std::nullopt;
            for (auto& row : table->children) std::swap(row.children[column], row.children[column - 1]);
            if (column < table->table_aligns.size()) std::swap(table->table_aligns[column], table->table_aligns[column - 1]);
            break;
        case DocumentTableEdit::MoveColumnRight:
            if (column + 1 >= column_count) return std::nullopt;
            for (auto& row : table->children) std::swap(row.children[column], row.children[column + 1]);
            if (column + 1 < table->table_aligns.size()) std::swap(table->table_aligns[column], table->table_aligns[column + 1]);
            break;
        case DocumentTableEdit::MoveColumnTo: {
            if (argument >= column_count || argument == column) return std::nullopt;
            for (auto& row : table->children) {
                auto cell = remove_block(row, column);
                if (!cell) return std::nullopt;
                insert_block(row, argument, std::move(*cell));
            }
            auto value = table->table_aligns[column];
            table->table_aligns.erase(table->table_aligns.begin() + static_cast<std::ptrdiff_t>(column));
            table->table_aligns.insert(table->table_aligns.begin() + static_cast<std::ptrdiff_t>(argument), value);
            break;
        }
        case DocumentTableEdit::SetColumnAlignment:
            if (column >= table->table_aligns.size()) table->table_aligns.resize(column_count, TableAlignment::None);
            table->table_aligns[column] = alignment;
            break;
        case DocumentTableEdit::Normalize: {
            auto columns = (std::max)(std::size_t{1}, column_count);
            for (auto& row : table->children) {
                while (row.children.size() < columns) row.children.push_back(make_cell());
                if (row.children.size() > columns) row.children.resize(columns);
            }
            table->table_aligns.resize(columns, TableAlignment::None);
            break;
        }
    }
    if (changed) ++after.revision;
    const auto target = TextSelection::caret(TextPosition{target_id, 0, TextAffinity::Downstream});
    return document_edit_detail::transaction(document, std::move(after), selection, target, DocumentTransactionReason::Structure);
}

} // namespace elmd

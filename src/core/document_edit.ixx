export module elmd.core.document_edit;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.document_position;

export namespace elmd {

enum class DocumentTransactionReason {
    InsertText,
    Delete,
    Paste,
    Format,
    Structure,
};

struct DocumentTransaction {
    EditorDocument before;
    EditorDocument after;
    DocumentSelection selection_before;
    DocumentSelection selection_after;
    DocumentTransactionReason reason = DocumentTransactionReason::Structure;
};

struct DocumentInvariantError {
    NodeId node_id{};
    std::string message;
};

class DocumentHistory {
public:
    explicit DocumentHistory(std::size_t capacity = 1000) : capacity_(capacity) {}

    void push(DocumentTransaction transaction) {
        undo_.push_back(std::move(transaction));
        redo_.clear();
        if (undo_.size() > capacity_) undo_.erase(undo_.begin());
    }

    std::optional<std::pair<EditorDocument, DocumentSelection>> undo() {
        if (undo_.empty()) return std::nullopt;
        auto transaction = std::move(undo_.back());
        undo_.pop_back();
        auto result = std::pair{transaction.before, transaction.selection_before};
        redo_.push_back(std::move(transaction));
        return result;
    }

    std::optional<std::pair<EditorDocument, DocumentSelection>> redo() {
        if (redo_.empty()) return std::nullopt;
        auto transaction = std::move(redo_.back());
        redo_.pop_back();
        auto result = std::pair{transaction.after, transaction.selection_after};
        undo_.push_back(std::move(transaction));
        return result;
    }

    void clear() {
        undo_.clear();
        redo_.clear();
    }

    bool has_undo() const { return !undo_.empty(); }
    bool has_redo() const { return !redo_.empty(); }

private:
    std::size_t capacity_;
    std::vector<DocumentTransaction> undo_;
    std::vector<DocumentTransaction> redo_;
};

namespace document_edit_detail {

inline void collect_max_id_inline(const InlineNode& node, std::uint64_t& value) {
    value = (std::max)(value, node.id.v);
    for (const auto& child : node.children) collect_max_id_inline(child, value);
}

inline void collect_max_id_block(const BlockNode& block, std::uint64_t& value) {
    value = (std::max)(value, block.id.v);
    for (const auto& child : block.children) collect_max_id_inline(child, value);
    for (const auto& child : block.quote_children) collect_max_id_block(child, value);
    for (const auto& item : block.list_items) {
        value = (std::max)(value, item.id.v);
        for (const auto& child : item.children) collect_max_id_block(child, value);
    }
    for (const auto& item : block.task_items) {
        value = (std::max)(value, item.id.v);
        for (const auto& child : item.children) collect_max_id_block(child, value);
    }
    for (const auto& cell : block.table_header) {
        value = (std::max)(value, cell.id.v);
        for (const auto& child : cell.children) collect_max_id_inline(child, value);
    }
    for (const auto& row : block.table_rows) {
        value = (std::max)(value, row.id.v);
        for (const auto& cell : row.cells) {
            value = (std::max)(value, cell.id.v);
            for (const auto& child : cell.children) collect_max_id_inline(child, value);
        }
    }
}

struct NodeAllocator {
    std::uint64_t next = 1;

    explicit NodeAllocator(const EditorDocument& document) {
        std::uint64_t maximum = 0;
        for (const auto& block : document.blocks) collect_max_id_block(block, maximum);
        next = maximum + 1;
    }

    NodeId allocate() { return NodeId(next++); }
};

inline std::size_t inline_length(const InlineNode& node) {
    return inline_text_content(node).size();
}

inline std::pair<InlineVec, InlineVec> split_inlines(const InlineVec& nodes, std::size_t offset, NodeAllocator& allocator);

inline std::pair<std::optional<InlineNode>, std::optional<InlineNode>> split_inline(
    const InlineNode& node,
    std::size_t offset,
    NodeAllocator& allocator) {
    const auto length = inline_length(node);
    if (offset == 0) return {std::nullopt, node};
    if (offset >= length) return {node, std::nullopt};

    const bool container = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
        || node.kind == InlineKind::Strike || node.kind == InlineKind::Span || node.kind == InlineKind::Link;
    if (container) {
        auto [left_children, right_children] = split_inlines(node.children, offset, allocator);
        std::optional<InlineNode> left;
        std::optional<InlineNode> right;
        if (!left_children.empty()) {
            left = node;
            left->children = std::move(left_children);
        }
        if (!right_children.empty()) {
            right = node;
            right->id = allocator.allocate();
            right->children = std::move(right_children);
        }
        return {std::move(left), std::move(right)};
    }

    if (node.kind == InlineKind::Text || node.kind == InlineKind::InlineCode
        || node.kind == InlineKind::InlineMath || node.kind == InlineKind::UnsupportedMarkup) {
        InlineNode left = node;
        InlineNode right = node;
        left.text = node.text.substr(0, offset);
        right.id = allocator.allocate();
        right.text = node.text.substr(offset);
        return {std::move(left), std::move(right)};
    }

    return offset * 2 < length
        ? std::pair<std::optional<InlineNode>, std::optional<InlineNode>>{std::nullopt, node}
        : std::pair<std::optional<InlineNode>, std::optional<InlineNode>>{node, std::nullopt};
}

inline std::pair<InlineVec, InlineVec> split_inlines(const InlineVec& nodes, std::size_t offset, NodeAllocator& allocator) {
    InlineVec left;
    InlineVec right;
    std::size_t consumed = 0;
    bool split = false;
    for (const auto& node : nodes) {
        const auto length = inline_length(node);
        if (split) {
            right.push_back(node);
        } else if (offset >= consumed + length) {
            left.push_back(node);
            consumed += length;
        } else {
            auto [left_node, right_node] = split_inline(node, offset - consumed, allocator);
            if (left_node) left.push_back(std::move(*left_node));
            if (right_node) right.push_back(std::move(*right_node));
            split = true;
        }
    }
    if (!split && offset == consumed) split = true;
    return {std::move(left), std::move(right)};
}

inline bool insert_text_in_inlines(InlineVec& nodes, std::size_t offset, std::u32string_view text, NodeAllocator& allocator) {
    std::size_t consumed = 0;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        const auto length = inline_length(node);
        if (offset > consumed + length) {
            consumed += length;
            continue;
        }
        const auto local = offset >= consumed ? offset - consumed : 0;
        const bool container = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
            || node.kind == InlineKind::Strike || node.kind == InlineKind::Span || node.kind == InlineKind::Link;
        if (container) {
            if (node.children.empty()) node.children.push_back(InlineNode::text_node(allocator.allocate(), std::u32string(text)));
            else insert_text_in_inlines(node.children, local, text, allocator);
            return true;
        }
        if (node.kind == InlineKind::Text || node.kind == InlineKind::InlineCode
            || node.kind == InlineKind::InlineMath || node.kind == InlineKind::UnsupportedMarkup) {
            node.text.insert((std::min)(local, node.text.size()), text);
            return true;
        }
        InlineNode inserted = InlineNode::text_node(allocator.allocate(), std::u32string(text));
        const auto insertion_index = local == 0 ? index : index + 1;
        nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(insertion_index), std::move(inserted));
        return true;
    }
    nodes.push_back(InlineNode::text_node(allocator.allocate(), std::u32string(text)));
    return true;
}

inline void erase_inline_range(InlineVec& nodes, std::size_t start, std::size_t count) {
    if (count == 0) return;
    std::size_t consumed = 0;
    for (std::size_t index = 0; index < nodes.size() && count > 0;) {
        auto& node = nodes[index];
        const auto length = inline_length(node);
        if (start >= consumed + length) {
            consumed += length;
            ++index;
            continue;
        }
        const auto local_start = start > consumed ? start - consumed : 0;
        const auto removable = (std::min)(count, length - local_start);
        const bool container = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
            || node.kind == InlineKind::Strike || node.kind == InlineKind::Span || node.kind == InlineKind::Link;
        if (container) {
            erase_inline_range(node.children, local_start, removable);
            if (node.children.empty()) {
                nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                consumed += inline_length(node);
                ++index;
            }
        } else if (node.kind == InlineKind::Text || node.kind == InlineKind::InlineCode
            || node.kind == InlineKind::InlineMath || node.kind == InlineKind::UnsupportedMarkup) {
            node.text.erase(local_start, removable);
            if (node.text.empty()) nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
            else {
                consumed += node.text.size();
                ++index;
            }
        } else {
            nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
        }
        count -= removable;
    }
}

inline bool block_is_empty_paragraph(const BlockNode& block) {
    return block.kind == BlockKind::Paragraph && block_inline_text_content(block.children).empty();
}

inline BlockNode empty_paragraph(NodeAllocator& allocator) {
    BlockNode paragraph;
    paragraph.id = allocator.allocate();
    paragraph.kind = BlockKind::Paragraph;
    return paragraph;
}

inline bool contains_block(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if (block.id == id) return true;
        if (contains_block(block.quote_children, id)) return true;
        for (const auto& item : block.list_items) if (contains_block(item.children, id)) return true;
        for (const auto& item : block.task_items) if (contains_block(item.children, id)) return true;
    }
    return false;
}

inline bool indent_list_item_in_blocks(BlockVec& blocks, NodeId id, NodeAllocator& allocator);
inline bool outdent_list_item_in_blocks(BlockVec& blocks, NodeId id, NodeAllocator& allocator);

inline BlockNode empty_nested_list(NodeAllocator& allocator, const BlockNode& source) {
    BlockNode nested;
    nested.id = allocator.allocate();
    nested.kind = source.kind;
    nested.list_ordered = source.list_ordered;
    nested.list_start = source.list_start;
    nested.list_delimiter = source.list_delimiter;
    return nested;
}

inline bool indent_in_list(BlockNode& list, NodeId id, NodeAllocator& allocator) {
    for (auto& item : list.list_items) {
        if (indent_list_item_in_blocks(item.children, id, allocator)) return true;
    }
    for (std::size_t index = 1; index < list.list_items.size(); ++index) {
        if (!contains_block(list.list_items[index].children, id)) continue;
        auto moved = std::move(list.list_items[index]);
        list.list_items.erase(list.list_items.begin() + static_cast<std::ptrdiff_t>(index));
        auto& previous = list.list_items[index - 1];
        if (!previous.children.empty()
            && previous.children.back().kind == BlockKind::List
            && previous.children.back().list_ordered == list.list_ordered
            && previous.children.back().list_delimiter == list.list_delimiter) {
            previous.children.back().list_items.push_back(std::move(moved));
        } else {
            auto nested = empty_nested_list(allocator, list);
            nested.list_items.push_back(std::move(moved));
            previous.children.push_back(std::move(nested));
        }
        return true;
    }
    return false;
}

inline bool indent_in_task_list(BlockNode& list, NodeId id, NodeAllocator& allocator) {
    for (auto& item : list.task_items) {
        if (indent_list_item_in_blocks(item.children, id, allocator)) return true;
    }
    for (std::size_t index = 1; index < list.task_items.size(); ++index) {
        if (!contains_block(list.task_items[index].children, id)) continue;
        auto moved = std::move(list.task_items[index]);
        list.task_items.erase(list.task_items.begin() + static_cast<std::ptrdiff_t>(index));
        auto& previous = list.task_items[index - 1];
        if (!previous.children.empty() && previous.children.back().kind == BlockKind::TaskList) {
            previous.children.back().task_items.push_back(std::move(moved));
        } else {
            auto nested = empty_nested_list(allocator, list);
            nested.task_items.push_back(std::move(moved));
            previous.children.push_back(std::move(nested));
        }
        return true;
    }
    return false;
}

inline bool indent_list_item_in_blocks(BlockVec& blocks, NodeId id, NodeAllocator& allocator) {
    for (auto& block : blocks) {
        if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
            if (indent_list_item_in_blocks(block.quote_children, id, allocator)) return true;
        }
        if (block.kind == BlockKind::List && indent_in_list(block, id, allocator)) return true;
        if (block.kind == BlockKind::TaskList && indent_in_task_list(block, id, allocator)) return true;
    }
    return false;
}

inline bool outdent_from_list(BlockNode& outer, NodeId id, NodeAllocator& allocator);
inline bool outdent_from_task_list(BlockNode& outer, NodeId id, NodeAllocator& allocator);

inline bool outdent_nested_list(
    BlockNode& outer,
    std::size_t parent_index,
    std::size_t child_index,
    std::size_t target_index,
    NodeAllocator& allocator) {
    auto& parent = outer.list_items[parent_index];
    auto& nested = parent.children[child_index];
    if (child_index == 0) {
        auto promoted = std::move(nested.list_items[target_index].children);
        nested.list_items.erase(nested.list_items.begin() + static_cast<std::ptrdiff_t>(target_index));
        if (nested.list_items.empty()) parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(child_index));
        parent.children.insert(
            parent.children.begin() + static_cast<std::ptrdiff_t>(child_index),
            std::make_move_iterator(promoted.begin()),
            std::make_move_iterator(promoted.end()));
        return true;
    }

    auto lifted = std::move(nested.list_items[target_index]);
    if (target_index + 1 < nested.list_items.size()) {
        auto trailing = empty_nested_list(allocator, nested);
        trailing.list_items.assign(
            std::make_move_iterator(nested.list_items.begin() + static_cast<std::ptrdiff_t>(target_index + 1)),
            std::make_move_iterator(nested.list_items.end()));
        lifted.children.push_back(std::move(trailing));
    }
    nested.list_items.erase(
        nested.list_items.begin() + static_cast<std::ptrdiff_t>(target_index),
        nested.list_items.end());
    if (nested.list_items.empty()) parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(child_index));
    outer.list_items.insert(outer.list_items.begin() + static_cast<std::ptrdiff_t>(parent_index + 1), std::move(lifted));
    return true;
}

inline bool outdent_nested_task_list(
    BlockNode& outer,
    std::size_t parent_index,
    std::size_t child_index,
    std::size_t target_index,
    NodeAllocator& allocator) {
    auto& parent = outer.task_items[parent_index];
    auto& nested = parent.children[child_index];
    if (child_index == 0) {
        auto promoted = std::move(nested.task_items[target_index].children);
        nested.task_items.erase(nested.task_items.begin() + static_cast<std::ptrdiff_t>(target_index));
        if (nested.task_items.empty()) parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(child_index));
        parent.children.insert(
            parent.children.begin() + static_cast<std::ptrdiff_t>(child_index),
            std::make_move_iterator(promoted.begin()),
            std::make_move_iterator(promoted.end()));
        return true;
    }

    auto lifted = std::move(nested.task_items[target_index]);
    if (target_index + 1 < nested.task_items.size()) {
        auto trailing = empty_nested_list(allocator, nested);
        trailing.task_items.assign(
            std::make_move_iterator(nested.task_items.begin() + static_cast<std::ptrdiff_t>(target_index + 1)),
            std::make_move_iterator(nested.task_items.end()));
        lifted.children.push_back(std::move(trailing));
    }
    nested.task_items.erase(
        nested.task_items.begin() + static_cast<std::ptrdiff_t>(target_index),
        nested.task_items.end());
    if (nested.task_items.empty()) parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(child_index));
    outer.task_items.insert(outer.task_items.begin() + static_cast<std::ptrdiff_t>(parent_index + 1), std::move(lifted));
    return true;
}

inline bool outdent_from_list(BlockNode& outer, NodeId id, NodeAllocator& allocator) {
    for (std::size_t parent_index = 0; parent_index < outer.list_items.size(); ++parent_index) {
        auto& children = outer.list_items[parent_index].children;
        for (std::size_t child_index = 0; child_index < children.size(); ++child_index) {
            auto& child = children[child_index];
            if (child.kind == BlockKind::List) {
                if (outdent_from_list(child, id, allocator)) return true;
                for (std::size_t target_index = 0; target_index < child.list_items.size(); ++target_index) {
                    if (contains_block(child.list_items[target_index].children, id)) {
                        return outdent_nested_list(outer, parent_index, child_index, target_index, allocator);
                    }
                }
            } else if (child.kind == BlockKind::BlockQuote || child.kind == BlockKind::Callout || child.kind == BlockKind::FootnoteDefinition) {
                if (outdent_list_item_in_blocks(child.quote_children, id, allocator)) return true;
            }
        }
    }
    return false;
}

inline bool outdent_from_task_list(BlockNode& outer, NodeId id, NodeAllocator& allocator) {
    for (std::size_t parent_index = 0; parent_index < outer.task_items.size(); ++parent_index) {
        auto& children = outer.task_items[parent_index].children;
        for (std::size_t child_index = 0; child_index < children.size(); ++child_index) {
            auto& child = children[child_index];
            if (child.kind == BlockKind::TaskList) {
                if (outdent_from_task_list(child, id, allocator)) return true;
                for (std::size_t target_index = 0; target_index < child.task_items.size(); ++target_index) {
                    if (contains_block(child.task_items[target_index].children, id)) {
                        return outdent_nested_task_list(outer, parent_index, child_index, target_index, allocator);
                    }
                }
            } else if (child.kind == BlockKind::BlockQuote || child.kind == BlockKind::Callout || child.kind == BlockKind::FootnoteDefinition) {
                if (outdent_list_item_in_blocks(child.quote_children, id, allocator)) return true;
            }
        }
    }
    return false;
}

inline bool outdent_list_item_in_blocks(BlockVec& blocks, NodeId id, NodeAllocator& allocator) {
    for (auto& block : blocks) {
        if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
            if (outdent_list_item_in_blocks(block.quote_children, id, allocator)) return true;
        }
        if (block.kind == BlockKind::List && outdent_from_list(block, id, allocator)) return true;
        if (block.kind == BlockKind::TaskList && outdent_from_task_list(block, id, allocator)) return true;
    }
    return false;
}

inline bool backspace_in_code_blocks(
    BlockVec& blocks,
    NodeId id,
    std::size_t offset,
    NodeAllocator& allocator,
    DocumentPosition& after) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto& block = blocks[index];
        if (block.id == id && block.kind == BlockKind::CodeBlock) {
            if (offset == 0 || offset > block.code_text.size()) return false;
            auto line_start = block.code_text.rfind(U'\n', offset - 1);
            line_start = line_start == std::u32string::npos ? 0 : line_start + 1;
            auto line_end = block.code_text.find(U'\n', line_start);
            if (line_end == std::u32string::npos) line_end = block.code_text.size();
            if (block.code_indented && offset == line_start && line_start == line_end) {
                const auto erase_start = line_start > 0 ? line_start - 1 : line_start;
                const auto erase_end = line_end < block.code_text.size() ? line_end + 1 : line_end;
                block.code_text.erase(erase_start, erase_end - erase_start);
                if (index + 1 < blocks.size() && blocks[index + 1].kind == BlockKind::Paragraph) {
                    after = DocumentPosition{blocks[index + 1].id, 0, TextAffinity::Upstream};
                } else {
                    auto paragraph = empty_paragraph(allocator);
                    after = DocumentPosition{paragraph.id, 0, TextAffinity::Upstream};
                    blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(paragraph));
                }
                return true;
            }
            block.code_text.erase(offset - 1, 1);
            after = DocumentPosition{block.id, offset - 1, TextAffinity::Upstream};
            return true;
        }
        if (backspace_in_code_blocks(block.quote_children, id, offset, allocator, after)) return true;
        for (auto& item : block.list_items) {
            if (backspace_in_code_blocks(item.children, id, offset, allocator, after)) return true;
        }
        for (auto& item : block.task_items) {
            if (backspace_in_code_blocks(item.children, id, offset, allocator, after)) return true;
        }
    }
    return false;
}

inline bool split_paragraph_in_blocks(BlockVec& blocks, NodeId id, std::size_t offset, NodeAllocator& allocator, DocumentPosition& after);

inline bool insert_text_in_blocks(BlockVec& blocks, NodeId id, std::size_t offset, std::u32string_view text, NodeAllocator& allocator) {
    for (auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) {
            return insert_text_in_inlines(block.children, offset, text, allocator);
        }
        if (insert_text_in_blocks(block.quote_children, id, offset, text, allocator)) return true;
        for (auto& item : block.list_items) if (insert_text_in_blocks(item.children, id, offset, text, allocator)) return true;
        for (auto& item : block.task_items) if (insert_text_in_blocks(item.children, id, offset, text, allocator)) return true;
    }
    return false;
}

inline bool erase_text_in_blocks(BlockVec& blocks, NodeId id, std::size_t start, std::size_t count) {
    for (auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) {
            erase_inline_range(block.children, start, count);
            return true;
        }
        if (block.id == id && block.kind == BlockKind::CodeBlock && start <= block.code_text.size()) {
            block.code_text.erase(start, (std::min)(count, block.code_text.size() - start));
            return true;
        }
        if (erase_text_in_blocks(block.quote_children, id, start, count)) return true;
        for (auto& item : block.list_items) if (erase_text_in_blocks(item.children, id, start, count)) return true;
        for (auto& item : block.task_items) if (erase_text_in_blocks(item.children, id, start, count)) return true;
    }
    return false;
}

inline bool erase_empty_inline_at(InlineVec& nodes, std::size_t offset) {
    std::size_t consumed = 0;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        const auto length = inline_length(node);
        const bool removable = node.kind == InlineKind::Emphasis || node.kind == InlineKind::Strong
            || node.kind == InlineKind::Strike || node.kind == InlineKind::InlineCode
            || node.kind == InlineKind::InlineMath || node.kind == InlineKind::Link;
        if (length == 0 && removable && consumed == offset) {
            nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }
        if (offset >= consumed && offset <= consumed + length && !node.children.empty()) {
            if (erase_empty_inline_at(node.children, offset - consumed)) return true;
        }
        consumed += length;
    }
    return false;
}

inline bool erase_empty_inline_in_blocks(BlockVec& blocks, NodeId id, std::size_t offset) {
    for (auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) return erase_empty_inline_at(block.children, offset);
        if (erase_empty_inline_in_blocks(block.quote_children, id, offset)) return true;
        for (auto& item : block.list_items) if (erase_empty_inline_in_blocks(item.children, id, offset)) return true;
        for (auto& item : block.task_items) if (erase_empty_inline_in_blocks(item.children, id, offset)) return true;
    }
    return false;
}

inline bool erase_adjacent_pair_in_inlines(InlineVec& nodes, std::size_t offset) {
    std::size_t consumed = 0;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        const auto length = inline_length(node);
        if (offset < consumed || offset > consumed + length) {
            consumed += length;
            continue;
        }
        const auto local = offset - consumed;
        if (!node.children.empty() && erase_adjacent_pair_in_inlines(node.children, local)) return true;
        if (node.kind == InlineKind::Text && local > 0 && local < node.text.size()) {
            const auto marker = node.text[local - 1];
            const bool pairable = marker == U'*' || marker == U'_' || marker == U'$' || marker == U'`' || marker == U'~';
            if (pairable && node.text[local] == marker) {
                node.text.erase(local - 1, 2);
                if (node.text.empty()) nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
                return true;
            }
        }
        return false;
    }
    return false;
}

inline bool erase_adjacent_pair_in_blocks(BlockVec& blocks, NodeId id, std::size_t offset) {
    for (auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) return erase_adjacent_pair_in_inlines(block.children, offset);
        if (erase_adjacent_pair_in_blocks(block.quote_children, id, offset)) return true;
        for (auto& item : block.list_items) if (erase_adjacent_pair_in_blocks(item.children, id, offset)) return true;
        for (auto& item : block.task_items) if (erase_adjacent_pair_in_blocks(item.children, id, offset)) return true;
    }
    return false;
}

inline std::optional<std::size_t> editable_length_in_blocks(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) return block_inline_text_content(block.children).size();
        if (block.id == id && block.kind == BlockKind::CodeBlock) return block.code_text.size();
        if (auto length = editable_length_in_blocks(block.quote_children, id)) return length;
        for (const auto& item : block.list_items) if (auto length = editable_length_in_blocks(item.children, id)) return length;
        for (const auto& item : block.task_items) if (auto length = editable_length_in_blocks(item.children, id)) return length;
    }
    return std::nullopt;
}

inline DocumentPosition merge_paragraphs(BlockNode& first, BlockNode& second) {
    const auto offset = block_inline_text_content(first.children).size();
    first.children.insert(
        first.children.end(),
        std::make_move_iterator(second.children.begin()),
        std::make_move_iterator(second.children.end()));
    return DocumentPosition{first.id, offset, TextAffinity::Downstream};
}

inline bool backspace_in_blocks(BlockVec& blocks, NodeId id, DocumentPosition& after);
inline bool delete_forward_in_blocks(BlockVec& blocks, NodeId id, DocumentPosition& after);

inline bool backspace_in_quote(BlockVec& owner, std::size_t quote_index, NodeId id, DocumentPosition& after) {
    auto& quote = owner[quote_index];
    for (std::size_t child_index = 0; child_index < quote.quote_children.size(); ++child_index) {
        auto& child = quote.quote_children[child_index];
        if (child.id != id || child.kind != BlockKind::Paragraph) continue;
        if (child_index > 0 && quote.quote_children[child_index - 1].kind == BlockKind::Paragraph) {
            after = merge_paragraphs(quote.quote_children[child_index - 1], child);
            quote.quote_children.erase(quote.quote_children.begin() + static_cast<std::ptrdiff_t>(child_index));
            return true;
        }
        BlockNode paragraph = std::move(child);
        after = DocumentPosition{paragraph.id, 0, TextAffinity::Downstream};
        if (quote.quote_children.size() == 1) {
            owner[quote_index] = std::move(paragraph);
        } else {
            quote.quote_children.erase(quote.quote_children.begin() + static_cast<std::ptrdiff_t>(child_index));
            owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(quote_index), std::move(paragraph));
        }
        return true;
    }
    return backspace_in_blocks(quote.quote_children, id, after);
}

inline bool backspace_in_list(BlockVec& owner, std::size_t list_index, NodeId id, DocumentPosition& after) {
    auto& list = owner[list_index];
    for (std::size_t item_index = 0; item_index < list.list_items.size(); ++item_index) {
        auto& item = list.list_items[item_index];
        for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
            auto& child = item.children[child_index];
            if (child.id != id || child.kind != BlockKind::Paragraph) continue;
            if (child_index > 0 && item.children[child_index - 1].kind == BlockKind::Paragraph) {
                after = merge_paragraphs(item.children[child_index - 1], child);
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(child_index));
                return true;
            }
            after = DocumentPosition{child.id, 0, TextAffinity::Downstream};
            if (item_index == 0) {
                auto lifted = std::move(item.children);
                list.list_items.erase(list.list_items.begin());
                if (list.list_ordered) ++list.list_start;
                if (list.list_items.empty()) {
                    owner.erase(owner.begin() + static_cast<std::ptrdiff_t>(list_index));
                }
                owner.insert(
                    owner.begin() + static_cast<std::ptrdiff_t>(list_index),
                    std::make_move_iterator(lifted.begin()),
                    std::make_move_iterator(lifted.end()));
            } else {
                auto moved = std::move(item.children);
                auto& previous = list.list_items[item_index - 1];
                previous.children.insert(
                    previous.children.end(),
                    std::make_move_iterator(moved.begin()),
                    std::make_move_iterator(moved.end()));
                list.list_items.erase(list.list_items.begin() + static_cast<std::ptrdiff_t>(item_index));
            }
            return true;
        }
        if (backspace_in_blocks(item.children, id, after)) return true;
    }
    return false;
}

inline bool backspace_in_task_list(BlockVec& owner, std::size_t list_index, NodeId id, DocumentPosition& after) {
    auto& list = owner[list_index];
    for (std::size_t item_index = 0; item_index < list.task_items.size(); ++item_index) {
        auto& item = list.task_items[item_index];
        for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
            auto& child = item.children[child_index];
            if (child.id != id || child.kind != BlockKind::Paragraph) continue;
            if (child_index > 0 && item.children[child_index - 1].kind == BlockKind::Paragraph) {
                after = merge_paragraphs(item.children[child_index - 1], child);
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(child_index));
                return true;
            }
            after = DocumentPosition{child.id, 0, TextAffinity::Downstream};
            if (item_index == 0) {
                auto lifted = std::move(item.children);
                list.task_items.erase(list.task_items.begin());
                if (list.task_items.empty()) owner.erase(owner.begin() + static_cast<std::ptrdiff_t>(list_index));
                owner.insert(
                    owner.begin() + static_cast<std::ptrdiff_t>(list_index),
                    std::make_move_iterator(lifted.begin()),
                    std::make_move_iterator(lifted.end()));
            } else {
                auto moved = std::move(item.children);
                auto& previous = list.task_items[item_index - 1];
                previous.children.insert(
                    previous.children.end(),
                    std::make_move_iterator(moved.begin()),
                    std::make_move_iterator(moved.end()));
                list.task_items.erase(list.task_items.begin() + static_cast<std::ptrdiff_t>(item_index));
            }
            return true;
        }
        if (backspace_in_blocks(item.children, id, after)) return true;
    }
    return false;
}

inline bool backspace_in_blocks(BlockVec& blocks, NodeId id, DocumentPosition& after) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto& block = blocks[index];
        if (block.id == id && block.kind == BlockKind::Paragraph) {
            if (index == 0 || blocks[index - 1].kind != BlockKind::Paragraph) return false;
            after = merge_paragraphs(blocks[index - 1], block);
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }
        if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
            if (backspace_in_quote(blocks, index, id, after)) return true;
        } else if (block.kind == BlockKind::List) {
            if (backspace_in_list(blocks, index, id, after)) return true;
        } else if (block.kind == BlockKind::TaskList) {
            if (backspace_in_task_list(blocks, index, id, after)) return true;
        }
    }
    return false;
}

inline bool delete_forward_in_quote(BlockVec& owner, std::size_t quote_index, NodeId id, DocumentPosition& after) {
    auto& children = owner[quote_index].quote_children;
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (children[index].id != id || children[index].kind != BlockKind::Paragraph) continue;
        if (index + 1 >= children.size() || children[index + 1].kind != BlockKind::Paragraph) return false;
        after = merge_paragraphs(children[index], children[index + 1]);
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(index + 1));
        return true;
    }
    return delete_forward_in_blocks(children, id, after);
}

inline bool delete_forward_in_list(BlockNode& list, NodeId id, DocumentPosition& after) {
    for (std::size_t item_index = 0; item_index < list.list_items.size(); ++item_index) {
        auto& item = list.list_items[item_index];
        for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
            auto& child = item.children[child_index];
            if (child.id != id || child.kind != BlockKind::Paragraph) continue;
            if (child_index + 1 < item.children.size() && item.children[child_index + 1].kind == BlockKind::Paragraph) {
                after = merge_paragraphs(child, item.children[child_index + 1]);
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1));
                return true;
            }
            if (child_index + 1 == item.children.size() && item_index + 1 < list.list_items.size()) {
                after = DocumentPosition{child.id, block_inline_text_content(child.children).size(), TextAffinity::Downstream};
                auto moved = std::move(list.list_items[item_index + 1].children);
                if (!moved.empty() && moved.front().kind == BlockKind::Paragraph) {
                    merge_paragraphs(child, moved.front());
                    moved.erase(moved.begin());
                }
                item.children.insert(item.children.end(), std::make_move_iterator(moved.begin()), std::make_move_iterator(moved.end()));
                list.list_items.erase(list.list_items.begin() + static_cast<std::ptrdiff_t>(item_index + 1));
                return true;
            }
            return false;
        }
        if (delete_forward_in_blocks(item.children, id, after)) return true;
    }
    return false;
}

inline bool delete_forward_in_task_list(BlockNode& list, NodeId id, DocumentPosition& after) {
    for (std::size_t item_index = 0; item_index < list.task_items.size(); ++item_index) {
        auto& item = list.task_items[item_index];
        for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
            auto& child = item.children[child_index];
            if (child.id != id || child.kind != BlockKind::Paragraph) continue;
            if (child_index + 1 < item.children.size() && item.children[child_index + 1].kind == BlockKind::Paragraph) {
                after = merge_paragraphs(child, item.children[child_index + 1]);
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1));
                return true;
            }
            if (child_index + 1 == item.children.size() && item_index + 1 < list.task_items.size()) {
                after = DocumentPosition{child.id, block_inline_text_content(child.children).size(), TextAffinity::Downstream};
                auto moved = std::move(list.task_items[item_index + 1].children);
                if (!moved.empty() && moved.front().kind == BlockKind::Paragraph) {
                    merge_paragraphs(child, moved.front());
                    moved.erase(moved.begin());
                }
                item.children.insert(item.children.end(), std::make_move_iterator(moved.begin()), std::make_move_iterator(moved.end()));
                list.task_items.erase(list.task_items.begin() + static_cast<std::ptrdiff_t>(item_index + 1));
                return true;
            }
            return false;
        }
        if (delete_forward_in_blocks(item.children, id, after)) return true;
    }
    return false;
}

inline bool delete_forward_in_blocks(BlockVec& blocks, NodeId id, DocumentPosition& after) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto& block = blocks[index];
        if (block.id == id && block.kind == BlockKind::Paragraph) {
            if (index + 1 >= blocks.size() || blocks[index + 1].kind != BlockKind::Paragraph) return false;
            after = merge_paragraphs(block, blocks[index + 1]);
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index + 1));
            return true;
        }
        if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
            if (delete_forward_in_quote(blocks, index, id, after)) return true;
        } else if (block.kind == BlockKind::List) {
            if (delete_forward_in_list(block, id, after)) return true;
        } else if (block.kind == BlockKind::TaskList) {
            if (delete_forward_in_task_list(block, id, after)) return true;
        }
    }
    return false;
}

inline void collect_paragraph_ids(const BlockVec& blocks, std::vector<NodeId>& ids) {
    for (const auto& block : blocks) {
        if (block.kind == BlockKind::Paragraph) ids.push_back(block.id);
        collect_paragraph_ids(block.quote_children, ids);
        for (const auto& item : block.list_items) collect_paragraph_ids(item.children, ids);
        for (const auto& item : block.task_items) collect_paragraph_ids(item.children, ids);
    }
}

inline BlockNode* find_paragraph_mut(BlockVec& blocks, NodeId id) {
    for (auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) return &block;
        if (auto* found = find_paragraph_mut(block.quote_children, id)) return found;
        for (auto& item : block.list_items) if (auto* found = find_paragraph_mut(item.children, id)) return found;
        for (auto& item : block.task_items) if (auto* found = find_paragraph_mut(item.children, id)) return found;
    }
    return nullptr;
}

struct DirectParagraphLocation {
    std::size_t item = 0;
    std::size_t child = 0;
};

inline std::optional<DirectParagraphLocation> direct_paragraph_in_list(const BlockNode& list, NodeId id) {
    for (std::size_t item_index = 0; item_index < list.list_items.size(); ++item_index) {
        for (std::size_t child_index = 0; child_index < list.list_items[item_index].children.size(); ++child_index) {
            const auto& child = list.list_items[item_index].children[child_index];
            if (child.id == id && child.kind == BlockKind::Paragraph) return DirectParagraphLocation{item_index, child_index};
        }
    }
    return std::nullopt;
}

inline std::optional<DirectParagraphLocation> direct_paragraph_in_task_list(const BlockNode& list, NodeId id) {
    for (std::size_t item_index = 0; item_index < list.task_items.size(); ++item_index) {
        for (std::size_t child_index = 0; child_index < list.task_items[item_index].children.size(); ++child_index) {
            const auto& child = list.task_items[item_index].children[child_index];
            if (child.id == id && child.kind == BlockKind::Paragraph) return DirectParagraphLocation{item_index, child_index};
        }
    }
    return std::nullopt;
}

inline bool delete_selection_in_same_list(
    BlockVec& blocks,
    NodeId first_id,
    std::size_t first_offset,
    NodeId last_id,
    std::size_t last_offset,
    std::size_t& join_offset) {
    for (auto& block : blocks) {
        if (delete_selection_in_same_list(block.quote_children, first_id, first_offset, last_id, last_offset, join_offset)) return true;
        for (auto& item : block.list_items) {
            if (delete_selection_in_same_list(item.children, first_id, first_offset, last_id, last_offset, join_offset)) return true;
        }
        for (auto& item : block.task_items) {
            if (delete_selection_in_same_list(item.children, first_id, first_offset, last_id, last_offset, join_offset)) return true;
        }

        if (block.kind == BlockKind::List) {
            auto first_location = direct_paragraph_in_list(block, first_id);
            auto last_location = direct_paragraph_in_list(block, last_id);
            if (!first_location || !last_location || first_location->item >= last_location->item) continue;
            auto& first_item = block.list_items[first_location->item];
            auto& last_item = block.list_items[last_location->item];
            auto& first = first_item.children[first_location->child];
            auto& last = last_item.children[last_location->child];
            const auto first_length = block_inline_text_content(first.children).size();
            join_offset = (std::min)(first_offset, first_length);
            erase_inline_range(first.children, join_offset, first_length - join_offset);
            erase_inline_range(last.children, 0, last_offset);
            first.children.insert(
                first.children.end(),
                std::make_move_iterator(last.children.begin()),
                std::make_move_iterator(last.children.end()));
            first_item.children.erase(
                first_item.children.begin() + static_cast<std::ptrdiff_t>(first_location->child + 1),
                first_item.children.end());
            first_item.children.insert(
                first_item.children.end(),
                std::make_move_iterator(last_item.children.begin() + static_cast<std::ptrdiff_t>(last_location->child + 1)),
                std::make_move_iterator(last_item.children.end()));
            block.list_items.erase(
                block.list_items.begin() + static_cast<std::ptrdiff_t>(first_location->item + 1),
                block.list_items.begin() + static_cast<std::ptrdiff_t>(last_location->item + 1));
            return true;
        }

        if (block.kind == BlockKind::TaskList) {
            auto first_location = direct_paragraph_in_task_list(block, first_id);
            auto last_location = direct_paragraph_in_task_list(block, last_id);
            if (!first_location || !last_location || first_location->item >= last_location->item) continue;
            auto& first_item = block.task_items[first_location->item];
            auto& last_item = block.task_items[last_location->item];
            auto& first = first_item.children[first_location->child];
            auto& last = last_item.children[last_location->child];
            const auto first_length = block_inline_text_content(first.children).size();
            join_offset = (std::min)(first_offset, first_length);
            erase_inline_range(first.children, join_offset, first_length - join_offset);
            erase_inline_range(last.children, 0, last_offset);
            first.children.insert(
                first.children.end(),
                std::make_move_iterator(last.children.begin()),
                std::make_move_iterator(last.children.end()));
            first_item.children.erase(
                first_item.children.begin() + static_cast<std::ptrdiff_t>(first_location->child + 1),
                first_item.children.end());
            first_item.children.insert(
                first_item.children.end(),
                std::make_move_iterator(last_item.children.begin() + static_cast<std::ptrdiff_t>(last_location->child + 1)),
                std::make_move_iterator(last_item.children.end()));
            block.task_items.erase(
                block.task_items.begin() + static_cast<std::ptrdiff_t>(first_location->item + 1),
                block.task_items.begin() + static_cast<std::ptrdiff_t>(last_location->item + 1));
            return true;
        }
    }
    return false;
}

struct RangeEraseState {
    NodeId first{};
    NodeId last{};
    bool active = false;
    bool done = false;
};

inline void erase_document_range(BlockVec& blocks, RangeEraseState& state) {
    for (std::size_t index = 0; index < blocks.size();) {
        auto& block = blocks[index];
        bool erase = false;
        if (block.kind == BlockKind::Paragraph) {
            if (block.id == state.first) {
                state.active = true;
            } else if (state.active && !state.done) {
                erase = true;
                if (block.id == state.last) state.done = true;
            }
        } else if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
            erase_document_range(block.quote_children, state);
            erase = block.quote_children.empty();
        } else if (block.kind == BlockKind::List) {
            for (std::size_t item_index = 0; item_index < block.list_items.size();) {
                erase_document_range(block.list_items[item_index].children, state);
                if (block.list_items[item_index].children.empty()) {
                    block.list_items.erase(block.list_items.begin() + static_cast<std::ptrdiff_t>(item_index));
                } else {
                    ++item_index;
                }
            }
            erase = block.list_items.empty();
        } else if (block.kind == BlockKind::TaskList) {
            for (std::size_t item_index = 0; item_index < block.task_items.size();) {
                erase_document_range(block.task_items[item_index].children, state);
                if (block.task_items[item_index].children.empty()) {
                    block.task_items.erase(block.task_items.begin() + static_cast<std::ptrdiff_t>(item_index));
                } else {
                    ++item_index;
                }
            }
            erase = block.task_items.empty();
        } else if (state.active && !state.done) {
            erase = true;
        }

        if (erase) {
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
}

inline bool split_direct_paragraph(BlockVec& blocks, std::size_t index, std::size_t offset, NodeAllocator& allocator, DocumentPosition& after) {
    auto& paragraph = blocks[index];
    auto [left, right] = split_inlines(paragraph.children, offset, allocator);
    paragraph.children = std::move(left);
    BlockNode next;
    next.id = allocator.allocate();
    next.kind = BlockKind::Paragraph;
    next.children = std::move(right);
    after = DocumentPosition{next.id, 0, TextAffinity::Downstream};
    blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(next));
    return true;
}

inline bool enter_quote(BlockVec& owner, std::size_t quote_index, NodeId id, std::size_t offset, NodeAllocator& allocator, DocumentPosition& after) {
    auto& quote = owner[quote_index];
    for (std::size_t child_index = 0; child_index < quote.quote_children.size(); ++child_index) {
        auto& child = quote.quote_children[child_index];
        if (child.id != id) continue;
        if (!block_is_empty_paragraph(child)) return split_direct_paragraph(quote.quote_children, child_index, offset, allocator, after);

        BlockNode paragraph = std::move(child);
        after = DocumentPosition{paragraph.id, 0, TextAffinity::Downstream};
        if (quote.quote_children.size() == 1) {
            owner[quote_index] = std::move(paragraph);
        } else if (child_index == 0) {
            quote.quote_children.erase(quote.quote_children.begin());
            owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(quote_index), std::move(paragraph));
        } else if (child_index + 1 == quote.quote_children.size()) {
            quote.quote_children.pop_back();
            owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(quote_index + 1), std::move(paragraph));
        } else {
            BlockNode trailing = quote;
            trailing.id = allocator.allocate();
            trailing.quote_children.assign(
                std::make_move_iterator(quote.quote_children.begin() + static_cast<std::ptrdiff_t>(child_index + 1)),
                std::make_move_iterator(quote.quote_children.end()));
            quote.quote_children.erase(quote.quote_children.begin() + static_cast<std::ptrdiff_t>(child_index), quote.quote_children.end());
            owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(quote_index + 1), std::move(paragraph));
            owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(quote_index + 2), std::move(trailing));
        }
        return true;
    }
    return split_paragraph_in_blocks(quote.quote_children, id, offset, allocator, after);
}

inline bool enter_list(BlockVec& owner, std::size_t list_index, NodeId id, std::size_t offset, NodeAllocator& allocator, DocumentPosition& after) {
    auto& list = owner[list_index];
    for (std::size_t item_index = 0; item_index < list.list_items.size(); ++item_index) {
        auto& item = list.list_items[item_index];
        for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
            auto& child = item.children[child_index];
            if (child.id != id) continue;
            const bool empty = block_is_empty_paragraph(child);
            if (empty && item.children.size() == 1) {
                BlockNode paragraph = std::move(child);
                after = DocumentPosition{paragraph.id, 0, TextAffinity::Downstream};
                if (list.list_items.size() == 1) {
                    owner[list_index] = std::move(paragraph);
                } else if (item_index == 0) {
                    list.list_items.erase(list.list_items.begin());
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index), std::move(paragraph));
                } else if (item_index + 1 == list.list_items.size()) {
                    list.list_items.pop_back();
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index + 1), std::move(paragraph));
                } else {
                    BlockNode trailing = list;
                    trailing.id = allocator.allocate();
                    trailing.list_items.assign(
                        std::make_move_iterator(list.list_items.begin() + static_cast<std::ptrdiff_t>(item_index + 1)),
                        std::make_move_iterator(list.list_items.end()));
                    list.list_items.erase(list.list_items.begin() + static_cast<std::ptrdiff_t>(item_index), list.list_items.end());
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index + 1), std::move(paragraph));
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index + 2), std::move(trailing));
                }
                return true;
            }

            ListItem next;
            next.id = allocator.allocate();
            next.marker = list.list_ordered ? std::u32string{} : item.marker;
            if (empty) {
                const auto move_from = child_index == 0 ? 1 : child_index;
                next.children.assign(
                    std::make_move_iterator(item.children.begin() + static_cast<std::ptrdiff_t>(move_from)),
                    std::make_move_iterator(item.children.end()));
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(move_from), item.children.end());
                if (next.children.empty()) next.children.push_back(empty_paragraph(allocator));
            } else {
                auto [left, right] = split_inlines(child.children, offset, allocator);
                child.children = std::move(left);
                BlockNode paragraph;
                paragraph.id = allocator.allocate();
                paragraph.kind = BlockKind::Paragraph;
                paragraph.children = std::move(right);
                next.children.push_back(std::move(paragraph));
                next.children.insert(next.children.end(),
                    std::make_move_iterator(item.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1)),
                    std::make_move_iterator(item.children.end()));
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1), item.children.end());
            }
            after = DocumentPosition{next.children.front().id, 0, TextAffinity::Downstream};
            list.list_items.insert(list.list_items.begin() + static_cast<std::ptrdiff_t>(item_index + 1), std::move(next));
            return true;
        }
        if (split_paragraph_in_blocks(item.children, id, offset, allocator, after)) return true;
    }
    return false;
}

inline bool enter_task_list(BlockVec& owner, std::size_t list_index, NodeId id, std::size_t offset, NodeAllocator& allocator, DocumentPosition& after) {
    auto& list = owner[list_index];
    for (std::size_t item_index = 0; item_index < list.task_items.size(); ++item_index) {
        auto& item = list.task_items[item_index];
        for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
            auto& child = item.children[child_index];
            if (child.id != id) continue;
            const bool empty = block_is_empty_paragraph(child);
            if (empty && item.children.size() == 1) {
                BlockNode paragraph = std::move(child);
                after = DocumentPosition{paragraph.id, 0, TextAffinity::Downstream};
                if (list.task_items.size() == 1) {
                    owner[list_index] = std::move(paragraph);
                } else if (item_index == 0) {
                    list.task_items.erase(list.task_items.begin());
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index), std::move(paragraph));
                } else if (item_index + 1 == list.task_items.size()) {
                    list.task_items.pop_back();
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index + 1), std::move(paragraph));
                } else {
                    BlockNode trailing = list;
                    trailing.id = allocator.allocate();
                    trailing.task_items.assign(
                        std::make_move_iterator(list.task_items.begin() + static_cast<std::ptrdiff_t>(item_index + 1)),
                        std::make_move_iterator(list.task_items.end()));
                    list.task_items.erase(list.task_items.begin() + static_cast<std::ptrdiff_t>(item_index), list.task_items.end());
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index + 1), std::move(paragraph));
                    owner.insert(owner.begin() + static_cast<std::ptrdiff_t>(list_index + 2), std::move(trailing));
                }
                return true;
            }

            TaskListItem next;
            next.id = allocator.allocate();
            next.checked = false;
            if (empty) {
                const auto move_from = child_index == 0 ? 1 : child_index;
                next.children.assign(
                    std::make_move_iterator(item.children.begin() + static_cast<std::ptrdiff_t>(move_from)),
                    std::make_move_iterator(item.children.end()));
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(move_from), item.children.end());
                if (next.children.empty()) next.children.push_back(empty_paragraph(allocator));
            } else {
                auto [left, right] = split_inlines(child.children, offset, allocator);
                child.children = std::move(left);
                BlockNode paragraph;
                paragraph.id = allocator.allocate();
                paragraph.kind = BlockKind::Paragraph;
                paragraph.children = std::move(right);
                next.children.push_back(std::move(paragraph));
                next.children.insert(next.children.end(),
                    std::make_move_iterator(item.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1)),
                    std::make_move_iterator(item.children.end()));
                item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1), item.children.end());
            }
            after = DocumentPosition{next.children.front().id, 0, TextAffinity::Downstream};
            list.task_items.insert(list.task_items.begin() + static_cast<std::ptrdiff_t>(item_index + 1), std::move(next));
            return true;
        }
        if (split_paragraph_in_blocks(item.children, id, offset, allocator, after)) return true;
    }
    return false;
}

inline bool split_paragraph_in_blocks(BlockVec& blocks, NodeId id, std::size_t offset, NodeAllocator& allocator, DocumentPosition& after) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto& block = blocks[index];
        if (block.id == id && block.kind == BlockKind::Paragraph) return split_direct_paragraph(blocks, index, offset, allocator, after);
        if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
            if (enter_quote(blocks, index, id, offset, allocator, after)) return true;
        } else if (block.kind == BlockKind::List) {
            if (enter_list(blocks, index, id, offset, allocator, after)) return true;
        } else if (block.kind == BlockKind::TaskList) {
            if (enter_task_list(blocks, index, id, offset, allocator, after)) return true;
        } else {
        }
    }
    return false;
}

inline void normalize_blocks(BlockVec& blocks, NodeAllocator& allocator) {
    for (auto& block : blocks) {
        if (block.kind == BlockKind::BlockQuote || block.kind == BlockKind::Callout || block.kind == BlockKind::FootnoteDefinition) {
            normalize_blocks(block.quote_children, allocator);
            if (block.quote_children.empty()) block.quote_children.push_back(empty_paragraph(allocator));
        }
        for (auto& item : block.list_items) {
            normalize_blocks(item.children, allocator);
            if (item.children.empty()) item.children.push_back(empty_paragraph(allocator));
        }
        for (auto& item : block.task_items) {
            normalize_blocks(item.children, allocator);
            if (item.children.empty()) item.children.push_back(empty_paragraph(allocator));
        }
    }
    if (blocks.empty()) blocks.push_back(empty_paragraph(allocator));
}

inline void validate_inline(const InlineNode& node, std::unordered_set<std::uint64_t>& ids, std::vector<DocumentInvariantError>& errors) {
    if (node.id.v == 0 || !ids.insert(node.id.v).second) errors.push_back({node.id, "inline node id is zero or duplicated"});
    for (const auto& child : node.children) validate_inline(child, ids, errors);
}

inline void validate_blocks(const BlockVec& blocks, std::unordered_set<std::uint64_t>& ids, std::vector<DocumentInvariantError>& errors) {
    for (const auto& block : blocks) {
        if (block.id.v == 0 || !ids.insert(block.id.v).second) errors.push_back({block.id, "block node id is zero or duplicated"});
        for (const auto& child : block.children) validate_inline(child, ids, errors);
        validate_blocks(block.quote_children, ids, errors);
        for (const auto& item : block.list_items) {
            if (item.id.v == 0 || !ids.insert(item.id.v).second) errors.push_back({item.id, "list item id is zero or duplicated"});
            if (item.children.empty()) errors.push_back({item.id, "list item has no content blocks"});
            validate_blocks(item.children, ids, errors);
        }
        for (const auto& item : block.task_items) {
            if (item.id.v == 0 || !ids.insert(item.id.v).second) errors.push_back({item.id, "task item id is zero or duplicated"});
            if (item.children.empty()) errors.push_back({item.id, "task item has no content blocks"});
            validate_blocks(item.children, ids, errors);
        }
    }
}

}

inline void normalize_document(EditorDocument& document) {
    document_edit_detail::NodeAllocator allocator(document);
    document_edit_detail::normalize_blocks(document.blocks, allocator);
}

inline std::vector<DocumentInvariantError> validate_document(const EditorDocument& document) {
    std::vector<DocumentInvariantError> errors;
    std::unordered_set<std::uint64_t> ids;
    if (document.blocks.empty()) errors.push_back({NodeId{}, "document has no blocks"});
    document_edit_detail::validate_blocks(document.blocks, ids, errors);
    return errors;
}

inline std::optional<DocumentTransaction> document_enter(const EditorDocument& document, const DocumentSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    EditorDocument after = document;
    document_edit_detail::NodeAllocator allocator(after);
    DocumentPosition target;
    if (!document_edit_detail::split_paragraph_in_blocks(
            after.blocks, selection.active.node_id, selection.active.offset, allocator, target)) return std::nullopt;
    normalize_document(after);
    ++after.revision;
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = DocumentSelection::caret(target);
    transaction.reason = DocumentTransactionReason::Structure;
    return transaction;
}

inline std::optional<DocumentTransaction> document_indent_list_item(
    const EditorDocument& document,
    const DocumentSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    EditorDocument after = document;
    document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::indent_list_item_in_blocks(after.blocks, selection.active.node_id, allocator)) return std::nullopt;
    normalize_document(after);
    ++after.revision;
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = selection;
    transaction.reason = DocumentTransactionReason::Structure;
    return transaction;
}

inline std::optional<DocumentTransaction> document_outdent_list_item(
    const EditorDocument& document,
    const DocumentSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    EditorDocument after = document;
    document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::outdent_list_item_in_blocks(after.blocks, selection.active.node_id, allocator)) return std::nullopt;
    normalize_document(after);
    ++after.revision;
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = selection;
    transaction.reason = DocumentTransactionReason::Structure;
    return transaction;
}

inline std::optional<DocumentTransaction> document_insert_text(
    const EditorDocument& document,
    const DocumentSelection& selection,
    std::u32string_view text) {
    if (!selection.is_caret() || text.empty()) return std::nullopt;
    EditorDocument after = document;
    document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::insert_text_in_blocks(
            after.blocks, selection.active.node_id, selection.active.offset, text, allocator)) return std::nullopt;
    normalize_document(after);
    ++after.revision;
    auto target = selection.active;
    target.offset += text.size();
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = DocumentSelection::caret(target);
    transaction.reason = DocumentTransactionReason::InsertText;
    return transaction;
}

inline std::optional<DocumentTransaction> document_delete_backward(
    const EditorDocument& document,
    const DocumentSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    EditorDocument after = document;
    auto target = selection.active;
    bool changed = false;
    document_edit_detail::NodeAllocator allocator(after);
    changed = document_edit_detail::backspace_in_code_blocks(after.blocks, target.node_id, target.offset, allocator, target);
    if (!changed && target.offset > 0) {
        changed = document_edit_detail::erase_adjacent_pair_in_blocks(after.blocks, target.node_id, target.offset);
        if (changed) {
            --target.offset;
        } else {
            const auto length_before = document_edit_detail::editable_length_in_blocks(after.blocks, target.node_id);
            changed = document_edit_detail::erase_text_in_blocks(after.blocks, target.node_id, target.offset - 1, 1);
            const auto length_after = document_edit_detail::editable_length_in_blocks(after.blocks, target.node_id);
            if (changed && length_before && length_after) {
                const auto removed = *length_before - *length_after;
                target.offset -= (std::min)(target.offset, removed);
            }
        }
    } else if (!changed) {
        changed = document_edit_detail::erase_empty_inline_in_blocks(after.blocks, target.node_id, target.offset);
        if (!changed) changed = document_edit_detail::backspace_in_blocks(after.blocks, target.node_id, target);
    }
    if (!changed) return std::nullopt;
    target.affinity = TextAffinity::Upstream;
    normalize_document(after);
    ++after.revision;
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = DocumentSelection::caret(target);
    transaction.reason = DocumentTransactionReason::Delete;
    return transaction;
}

inline std::optional<DocumentTransaction> document_delete_forward(
    const EditorDocument& document,
    const DocumentSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    EditorDocument after = document;
    auto target = selection.active;
    const auto length = document_edit_detail::editable_length_in_blocks(after.blocks, target.node_id);
    if (!length) return std::nullopt;
    bool changed = false;
    if (target.offset < *length) {
        changed = document_edit_detail::erase_text_in_blocks(after.blocks, target.node_id, target.offset, 1);
    } else {
        changed = document_edit_detail::delete_forward_in_blocks(after.blocks, target.node_id, target);
    }
    if (!changed) return std::nullopt;
    normalize_document(after);
    ++after.revision;
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = DocumentSelection::caret(target);
    transaction.reason = DocumentTransactionReason::Delete;
    return transaction;
}

inline std::optional<DocumentTransaction> document_delete_selection(
    const EditorDocument& document,
    const DocumentSelection& selection) {
    if (selection.is_caret()) return std::nullopt;
    EditorDocument after = document;
    auto anchor = selection.anchor;
    auto active = selection.active;

    if (anchor.node_id == active.node_id) {
        const auto start = (std::min)(anchor.offset, active.offset);
        const auto end = (std::max)(anchor.offset, active.offset);
        if (start == end || !document_edit_detail::erase_text_in_blocks(after.blocks, anchor.node_id, start, end - start)) return std::nullopt;
        ++after.revision;
        DocumentTransaction transaction;
        transaction.before = document;
        transaction.after = std::move(after);
        transaction.selection_before = selection;
        transaction.selection_after = DocumentSelection::caret(DocumentPosition{anchor.node_id, start, TextAffinity::Downstream});
        transaction.reason = DocumentTransactionReason::Delete;
        return transaction;
    }

    std::vector<NodeId> paragraph_ids;
    document_edit_detail::collect_paragraph_ids(after.blocks, paragraph_ids);
    auto anchor_order = std::find(paragraph_ids.begin(), paragraph_ids.end(), anchor.node_id);
    auto active_order = std::find(paragraph_ids.begin(), paragraph_ids.end(), active.node_id);
    if (anchor_order == paragraph_ids.end() || active_order == paragraph_ids.end()) return std::nullopt;
    if (anchor_order > active_order) {
        std::swap(anchor, active);
        std::swap(anchor_order, active_order);
    }

    std::size_t same_list_join = 0;
    if (document_edit_detail::delete_selection_in_same_list(
            after.blocks,
            anchor.node_id,
            anchor.offset,
            active.node_id,
            active.offset,
            same_list_join)) {
        normalize_document(after);
        ++after.revision;
        DocumentTransaction transaction;
        transaction.before = document;
        transaction.after = std::move(after);
        transaction.selection_before = selection;
        transaction.selection_after = DocumentSelection::caret(
            DocumentPosition{anchor.node_id, same_list_join, TextAffinity::Downstream});
        transaction.reason = DocumentTransactionReason::Delete;
        return transaction;
    }

    auto* first = document_edit_detail::find_paragraph_mut(after.blocks, anchor.node_id);
    auto* last = document_edit_detail::find_paragraph_mut(after.blocks, active.node_id);
    if (!first || !last) return std::nullopt;
    const auto first_length = block_inline_text_content(first->children).size();
    const auto join_offset = (std::min)(anchor.offset, first_length);
    document_edit_detail::erase_inline_range(first->children, join_offset, first_length - join_offset);
    document_edit_detail::erase_inline_range(last->children, 0, active.offset);
    auto tail = std::move(last->children);

    document_edit_detail::RangeEraseState state;
    state.first = anchor.node_id;
    state.last = active.node_id;
    document_edit_detail::erase_document_range(after.blocks, state);
    if (!state.active || !state.done) return std::nullopt;
    first = document_edit_detail::find_paragraph_mut(after.blocks, anchor.node_id);
    if (!first) return std::nullopt;
    first->children.insert(
        first->children.end(),
        std::make_move_iterator(tail.begin()),
        std::make_move_iterator(tail.end()));
    normalize_document(after);
    ++after.revision;
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = DocumentSelection::caret(DocumentPosition{first->id, join_offset, TextAffinity::Downstream});
    transaction.reason = DocumentTransactionReason::Delete;
    return transaction;
}

}

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
        if (erase_text_in_blocks(block.quote_children, id, start, count)) return true;
        for (auto& item : block.list_items) if (erase_text_in_blocks(item.children, id, start, count)) return true;
        for (auto& item : block.task_items) if (erase_text_in_blocks(item.children, id, start, count)) return true;
    }
    return false;
}

inline std::optional<std::size_t> paragraph_length_in_blocks(const BlockVec& blocks, NodeId id) {
    for (const auto& block : blocks) {
        if (block.id == id && block.kind == BlockKind::Paragraph) return block_inline_text_content(block.children).size();
        if (auto length = paragraph_length_in_blocks(block.quote_children, id)) return length;
        for (const auto& item : block.list_items) if (auto length = paragraph_length_in_blocks(item.children, id)) return length;
        for (const auto& item : block.task_items) if (auto length = paragraph_length_in_blocks(item.children, id)) return length;
    }
    return std::nullopt;
}

inline std::optional<DocumentPosition> merge_top_level_paragraph_backward(BlockVec& blocks, NodeId id) {
    for (std::size_t index = 1; index < blocks.size(); ++index) {
        if (blocks[index].id != id || blocks[index].kind != BlockKind::Paragraph || blocks[index - 1].kind != BlockKind::Paragraph) continue;
        const auto previous_length = block_inline_text_content(blocks[index - 1].children).size();
        blocks[index - 1].children.insert(
            blocks[index - 1].children.end(),
            std::make_move_iterator(blocks[index].children.begin()),
            std::make_move_iterator(blocks[index].children.end()));
        const auto target_id = blocks[index - 1].id;
        blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
        return DocumentPosition{target_id, previous_length, TextAffinity::Downstream};
    }
    return std::nullopt;
}

inline std::optional<DocumentPosition> merge_top_level_paragraph_forward(BlockVec& blocks, NodeId id) {
    for (std::size_t index = 0; index + 1 < blocks.size(); ++index) {
        if (blocks[index].id != id || blocks[index].kind != BlockKind::Paragraph || blocks[index + 1].kind != BlockKind::Paragraph) continue;
        const auto offset = block_inline_text_content(blocks[index].children).size();
        blocks[index].children.insert(
            blocks[index].children.end(),
            std::make_move_iterator(blocks[index + 1].children.begin()),
            std::make_move_iterator(blocks[index + 1].children.end()));
        blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index + 1));
        return DocumentPosition{id, offset, TextAffinity::Downstream};
    }
    return std::nullopt;
}

inline std::optional<std::size_t> top_level_paragraph_index(const BlockVec& blocks, NodeId id) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].id == id && blocks[index].kind == BlockKind::Paragraph) return index;
    }
    return std::nullopt;
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
    if (target.offset > 0) {
        changed = document_edit_detail::erase_text_in_blocks(after.blocks, target.node_id, target.offset - 1, 1);
        if (changed) --target.offset;
    } else if (auto merged = document_edit_detail::merge_top_level_paragraph_backward(after.blocks, target.node_id)) {
        target = *merged;
        changed = true;
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

inline std::optional<DocumentTransaction> document_delete_forward(
    const EditorDocument& document,
    const DocumentSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    EditorDocument after = document;
    auto target = selection.active;
    const auto length = document_edit_detail::paragraph_length_in_blocks(after.blocks, target.node_id);
    if (!length) return std::nullopt;
    bool changed = false;
    if (target.offset < *length) {
        changed = document_edit_detail::erase_text_in_blocks(after.blocks, target.node_id, target.offset, 1);
    } else if (auto merged = document_edit_detail::merge_top_level_paragraph_forward(after.blocks, target.node_id)) {
        target = *merged;
        changed = true;
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

    auto anchor_index = document_edit_detail::top_level_paragraph_index(after.blocks, anchor.node_id);
    auto active_index = document_edit_detail::top_level_paragraph_index(after.blocks, active.node_id);
    if (!anchor_index || !active_index) return std::nullopt;
    if (*anchor_index > *active_index) {
        std::swap(anchor_index, active_index);
        std::swap(anchor, active);
    }
    auto& first = after.blocks[*anchor_index];
    auto& last = after.blocks[*active_index];
    const auto first_length = block_inline_text_content(first.children).size();
    document_edit_detail::erase_inline_range(first.children, (std::min)(anchor.offset, first_length), first_length - (std::min)(anchor.offset, first_length));
    document_edit_detail::erase_inline_range(last.children, 0, active.offset);
    first.children.insert(
        first.children.end(),
        std::make_move_iterator(last.children.begin()),
        std::make_move_iterator(last.children.end()));
    after.blocks.erase(
        after.blocks.begin() + static_cast<std::ptrdiff_t>(*anchor_index + 1),
        after.blocks.begin() + static_cast<std::ptrdiff_t>(*active_index + 1));
    normalize_document(after);
    ++after.revision;
    DocumentTransaction transaction;
    transaction.before = document;
    transaction.after = std::move(after);
    transaction.selection_before = selection;
    transaction.selection_after = DocumentSelection::caret(DocumentPosition{first.id, anchor.offset, TextAffinity::Downstream});
    transaction.reason = DocumentTransactionReason::Delete;
    return transaction;
}

}

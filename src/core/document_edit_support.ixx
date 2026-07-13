export module elmd.core.document_edit_support;
export import elmd.core.document_transaction;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
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
    if (text_block(block.kind) || block.kind == BlockKind::TableCell) return block.inline_content.source.size();
    if (block.kind == BlockKind::CodeBlock) return block.code_text.size();
    if (block.kind == BlockKind::MathBlock) return block.tex.size();
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
    if (auto length = local_position_length(block)) return TextPosition{block.id, *length, TextAffinity::Upstream};
    for (auto child = block.children.rbegin(); child != block.children.rend(); ++child) {
        if (auto position = last_editable_position(*child)) return position;
    }
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
        const auto caret = offset + replacement.size();
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

inline std::optional<TextPosition> exit_empty_indented_code(
    EditorDocument& document,
    TextPosition position,
    NodeAllocator& allocator) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto* block = block_at_path(document.root, *path);
    if (!block || block->kind != BlockKind::CodeBlock || !block->code_indented) return std::nullopt;

    const auto offset = (std::min)(position.source_offset, block->code_text.size());
    auto line_start = offset == 0 ? std::u32string::npos : block->code_text.rfind(U'\n', offset - 1);
    line_start = line_start == std::u32string::npos ? 0 : line_start + 1;
    auto line_end = block->code_text.find(U'\n', offset);
    if (line_end == std::u32string::npos) line_end = block->code_text.size();
    for (auto index = line_start; index < line_end; ++index) {
        if (block->code_text[index] != U' ' && block->code_text[index] != U'\t') return std::nullopt;
    }

    // The blank line is the exit trigger, not content that should survive in
    // either code block. Remove its preceding line separator as well so the
    // leading block does not retain a visually empty trailing line.
    const auto before_end = line_start > 0 && block->code_text[line_start - 1] == U'\n'
        ? line_start - 1
        : line_start;
    auto before = block->code_text.substr(0, before_end);
    auto after_start = line_end < block->code_text.size() ? line_end + 1 : line_end;
    auto after = block->code_text.substr(after_start);
    auto parent_path = *path;
    const auto block_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent) return std::nullopt;

    auto paragraph = empty_paragraph(allocator, document);
    const auto target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    if (before.empty()) {
        auto trailing = std::move(parent->children[block_index]);
        trailing.code_text = std::move(after);
        parent->children[block_index] = std::move(paragraph);
        if (!trailing.code_text.empty()) insert_block(*parent, block_index + 1, std::move(trailing));
        return target;
    }

    parent->children[block_index].code_text = std::move(before);
    insert_block(*parent, block_index + 1, std::move(paragraph));
    if (!after.empty()) {
        auto trailing = parent->children[block_index];
        trailing.id = allocator.allocate();
        trailing.code_text = std::move(after);
        insert_block(*parent, block_index + 2, std::move(trailing));
    }
    return target;
}

inline std::optional<TextPosition> exit_empty_block_quote(
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
    const auto target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    BlockVec trailing_children;
    trailing_children.insert(trailing_children.end(),
        std::make_move_iterator(quote.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1)),
        std::make_move_iterator(quote.children.end()));
    quote.children.erase(quote.children.begin() + static_cast<std::ptrdiff_t>(child_index), quote.children.end());

    std::optional<BlockNode> trailing_quote;
    if (!trailing_children.empty()) {
        BlockNode value;
        value.id = allocator.allocate();
        value.kind = BlockKind::BlockQuote;
        value.children = std::move(trailing_children);
        trailing_quote = std::move(value);
    }
    if (quote.children.empty()) {
        parent->children[quote_index] = std::move(paragraph);
        if (trailing_quote) insert_block(*parent, quote_index + 1, std::move(*trailing_quote));
    } else {
        insert_block(*parent, quote_index + 1, std::move(paragraph));
        if (trailing_quote) insert_block(*parent, quote_index + 2, std::move(*trailing_quote));
    }
    return target;
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
            if (backward && index > 0 && blocks[index].kind == BlockKind::Paragraph
                && blocks[index].inline_content.source.empty()) {
                if (auto previous = last_editable_position(blocks[index - 1])) {
                    blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
                    target = *previous;
                    return true;
                }
            }
            if (!backward && index + 1 < blocks.size() && text_block(blocks[index + 1].kind)) {
                const auto offset = blocks[index].inline_content.source.size();
                blocks[index].inline_content.source += blocks[index + 1].inline_content.source;
                reparse(blocks[index].inline_content, document, allocator);
                blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index + 1));
                target = TextPosition{blocks[index].id, offset, TextAffinity::Downstream};
                return true;
            }
            if (!backward && index + 1 < blocks.size() && blocks[index].kind == BlockKind::Paragraph
                && blocks[index].inline_content.source.empty()) {
                if (auto next = first_editable_position(blocks[index + 1])) {
                    blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
                    target = *next;
                    return true;
                }
            }
        }
        if (join_adjacent(blocks[index].children, id, backward, document, allocator, target)) return true;
    }
    return false;
}

inline bool remove_atomic(BlockVec& blocks, NodeId id, NodeAllocator& allocator, const EditorDocument& owner, TextPosition& target) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].id == id && atomic_block(blocks[index].kind)) {
            if (index + 1 < blocks.size()) {
                if (auto next = first_editable_position(blocks[index + 1])) target = *next;
                else {
                    blocks[index] = empty_paragraph(allocator, owner);
                    target = {blocks[index].id, 0, TextAffinity::Downstream};
                    return true;
                }
            } else if (index > 0) {
                if (auto previous = last_editable_position(blocks[index - 1])) target = *previous;
                else {
                    blocks[index] = empty_paragraph(allocator, owner);
                    target = {blocks[index].id, 0, TextAffinity::Downstream};
                    return true;
                }
            } else {
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
    return make_document_transaction(before, std::move(after), selection_before, selection_after, reason);
}

} // namespace document_edit_detail

} // namespace elmd

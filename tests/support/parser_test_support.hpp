#pragma once

namespace {

const BlockNode* first_block(const BlockVec& blocks, BlockKind kind) {
    for (const auto& block : blocks) {
        if (block.kind == kind) return &block;
        if (auto* nested = first_block(block.children, kind)) return nested;
    }
    return nullptr;
}

bool has_inline(const InlineDocument& document, InlineCstKind kind) {
    return inline_contains_kind(document, kind);
}

const InlineCstNode* first_inline(const InlineCstNodes& nodes, InlineCstKind kind) {
    for (const auto& node : nodes) {
        if (node.kind == kind) return &node;
        if (auto* nested = first_inline(node.children, kind)) return nested;
    }
    return nullptr;
}

const InlineCstNode* first_inline(const InlineDocument& document, InlineCstKind kind) {
    return first_inline(document.tree.nodes, kind);
}

void expect_lossless(const InlineDocument& document) {
    expect(fatal(bool(tokens_partition_source(document.tree, document.source.size()))));
    expect(fatal(bool(roots_partition_source(document.tree, document.source.size()))));
    expect(fatal(bool(flatten_tokens(document.tree, document.source) == document.source)));
    expect(fatal(bool(serialize_lossless(document.tree, document.source) == document.source)));
}

} // namespace

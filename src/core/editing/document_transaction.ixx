// elmd.core.document_transaction — reversible document operations produced by
// editing commands. Commands apply these operations directly to the
// authoritative document; transactions never retain complete document
// snapshots.
export module elmd.core.document_transaction;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.text_edit;

export namespace elmd {

enum class DocumentTransactionReason { InsertText, Delete, Paste, Format, Structure };

struct DocumentTextOperation {
    TextEdit forward;
    TextEdit inverse;
};

enum class DocumentTreeEditKind { Insert, Remove, Move, UpdatePayload };

struct DocumentTreeEdit {
    DocumentTreeEditKind kind = DocumentTreeEditKind::Insert;
    NodeId parent_id{};
    NodeId other_parent_id{};
    NodeId moved_id{};
    std::size_t index = 0;
    std::size_t other_index = 0;
    BlockNode before;
    BlockNode after;
};

using DocumentOperation = std::variant<DocumentTextOperation, DocumentTreeEdit>;

struct DocumentTransaction {
    std::vector<DocumentOperation> operations;
    TextSelection selection_before;
    TextSelection selection_after;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
    DocumentTransactionReason reason = DocumentTransactionReason::Structure;
};

namespace document_transaction_detail {

inline BlockNode payload_shell(const BlockNode& source) {
    auto shell = source;
    shell.children.clear();
    shell.inline_content = {};
    shell.block_source = {};
    return shell;
}

} // namespace document_transaction_detail

inline DocumentTransaction make_recorded_document_transaction(
    std::vector<DocumentOperation> operations,
    TextSelection selection_before,
    TextSelection selection_after,
    std::uint64_t revision_before,
    std::uint64_t revision_after,
    DocumentTransactionReason reason) {
    DocumentTransaction transaction;
    transaction.operations = std::move(operations);
    transaction.selection_before = selection_before;
    transaction.selection_after = selection_after;
    transaction.revision_before = revision_before;
    transaction.revision_after = revision_after;
    transaction.reason = reason;
    return transaction;
}

} // namespace elmd

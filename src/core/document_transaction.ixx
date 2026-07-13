// elmd.core.document_transaction — reversible document operations produced by
// editing commands. History consumes this operation log directly; transactions
// never retain a complete "before" EditorDocument snapshot.
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
    std::size_t index = 0;
    std::size_t other_index = 0;
    BlockNode before;
    BlockNode after;
};

using DocumentOperation = std::variant<DocumentTextOperation, DocumentTreeEdit>;

struct DocumentTransaction {
    EditorDocument after;
    std::vector<DocumentOperation> operations;
    TextSelection selection_before;
    TextSelection selection_after;
    std::uint64_t revision_before = 0;
    DocumentTransactionReason reason = DocumentTransactionReason::Structure;
};

namespace document_transaction_detail {

inline BlockNode payload_shell(const BlockNode& source) {
    auto shell = source;
    shell.children.clear();
    shell.inline_content = {};
    shell.code_text.clear();
    shell.tex.clear();
    return shell;
}

} // namespace document_transaction_detail

inline DocumentTransaction make_recorded_document_transaction(
    EditorDocument after,
    std::vector<DocumentOperation> operations,
    TextSelection selection_before,
    TextSelection selection_after,
    std::uint64_t revision_before,
    DocumentTransactionReason reason) {
    DocumentTransaction transaction;
    transaction.after = std::move(after);
    transaction.operations = std::move(operations);
    transaction.selection_before = selection_before;
    transaction.selection_after = selection_after;
    transaction.revision_before = revision_before;
    transaction.reason = reason;
    return transaction;
}

} // namespace elmd

// folia.core.document_history — reversible source and block-tree operation log.
//
// Normal undo/redo stores only the edits that changed the document. It never
// retains complete EditorDocument snapshots.
export module folia.core.document_history;
import std;
import folia.core.document;
import folia.core.document_operation_apply;
import folia.core.document_transaction;
import folia.core.text_edit;

export namespace folia {

struct DocumentHistoryEntry {
    std::vector<DocumentOperation> operations;
    TextSelection selection_before;
    TextSelection selection_after;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
};

namespace document_history_detail {

inline DocumentHistoryEntry make_entry(const DocumentTransaction& transaction) {
    DocumentHistoryEntry entry;
    entry.operations = transaction.operations;
    entry.selection_before = transaction.selection_before;
    entry.selection_after = transaction.selection_after;
    entry.revision_before = transaction.revision_before;
    entry.revision_after = transaction.revision_after;
    return entry;
}

inline bool apply_entry(EditorDocument& document, TextSelection& selection,
                        const DocumentHistoryEntry& entry, bool forward) {
    if (!apply_document_operations(document, entry.operations, forward)) return false;
    if (forward) {
        document.revision = entry.revision_after;
        selection = entry.selection_after;
    } else {
        document.revision = entry.revision_before;
        selection = entry.selection_before;
    }
    return true;
}

} // namespace document_history_detail

class DocumentHistory {
public:
    explicit DocumentHistory(std::size_t capacity = 1000) : capacity_(capacity) {}

    void push(const DocumentTransaction& transaction) {
        undo_.push_back(document_history_detail::make_entry(transaction));
        redo_.clear();
        if (undo_.size() > capacity_) undo_.erase(undo_.begin());
    }

    bool undo(EditorDocument& document, TextSelection& selection) {
        if (undo_.empty()) return false;
        auto entry = std::move(undo_.back());
        undo_.pop_back();
        if (!document_history_detail::apply_entry(document, selection, entry, false)) {
            undo_.push_back(std::move(entry));
            return false;
        }
        redo_.push_back(std::move(entry));
        return true;
    }

    bool redo(EditorDocument& document, TextSelection& selection) {
        if (redo_.empty()) return false;
        auto entry = std::move(redo_.back());
        redo_.pop_back();
        if (!document_history_detail::apply_entry(document, selection, entry, true)) {
            redo_.push_back(std::move(entry));
            return false;
        }
        undo_.push_back(std::move(entry));
        return true;
    }

    void clear() { undo_.clear(); redo_.clear(); }
    bool has_undo() const { return !undo_.empty(); }
    bool has_redo() const { return !redo_.empty(); }
    const DocumentHistoryEntry* next_undo() const { return undo_.empty() ? nullptr : &undo_.back(); }
    const DocumentHistoryEntry* next_redo() const { return redo_.empty() ? nullptr : &redo_.back(); }

private:
    std::size_t capacity_;
    std::vector<DocumentHistoryEntry> undo_;
    std::vector<DocumentHistoryEntry> redo_;
};

} // namespace folia

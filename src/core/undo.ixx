// elmd.core.undo — UndoManager.
export module elmd.core.undo;
import std;
import elmd.core.transaction;
import elmd.core.selection;

export namespace elmd {

struct UndoManager {
    std::vector<Transaction> undo_stack;
    std::vector<Transaction> redo_stack;
    std::size_t max_undo = 1000;

    UndoManager() = default;
    explicit UndoManager(std::size_t m) : max_undo(m) {}

    void push(Transaction t) {
        redo_stack.clear();
        undo_stack.push_back(std::move(t));
        if (undo_stack.size() > max_undo) undo_stack.erase(undo_stack.begin());
    }
    std::optional<Transaction> undo() {
        if (undo_stack.empty()) return std::nullopt;
        Transaction t = std::move(undo_stack.back()); undo_stack.pop_back();
        redo_stack.push_back(t);
        return t;
    }
    std::optional<Transaction> redo() {
        if (redo_stack.empty()) return std::nullopt;
        Transaction t = std::move(redo_stack.back()); redo_stack.pop_back();
        undo_stack.push_back(t);
        return t;
    }
    void clear() { undo_stack.clear(); redo_stack.clear(); }
    bool has_undo() const { return !undo_stack.empty(); }
    bool has_redo() const { return !redo_stack.empty(); }
};

} // namespace elmd
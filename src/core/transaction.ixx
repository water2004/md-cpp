// elmd.core.transaction — editor-level Transaction and TextEdit.
// Carries `original` for reversible undo (HANDOFF invariant #4/#6).
export module elmd.core.transaction;
import std;
import elmd.core.types;
import elmd.core.selection;
import elmd.core.buffer;

export namespace elmd {

struct TextEdit {
    TextRange<CharOffset> range;
    std::u32string replacement;
    std::u32string original; // populated when the editor applies the edit
};

enum class TransactionReason {
    Typing, Delete, Paste, FormatCommand, StructuralCommand, Undo, Redo,
};

struct Transaction {
    std::uint64_t revision_before = 0;
    std::vector<TextEdit> edits;
    Selection selection_before;
    Selection selection_after;
    TransactionReason reason = TransactionReason::Typing;

    Transaction() = default;
    Transaction(std::uint64_t rev, Selection before, Selection after, TransactionReason r)
        : revision_before(rev), selection_before(std::move(before)), selection_after(std::move(after)), reason(r) {}

    Transaction& with_edit(TextRange<CharOffset> r, std::u32string replacement) {
        TextEdit e; e.range = r; e.replacement = std::move(replacement); edits.push_back(std::move(e)); return *this;
    }
    Transaction& with_edit_original(TextRange<CharOffset> r, std::u32string replacement, std::u32string original) {
        TextEdit e; e.range = r; e.replacement = std::move(replacement); e.original = std::move(original); edits.push_back(std::move(e)); return *this;
    }
    bool is_empty() const { return edits.empty(); }

    // Forward delta for apply via TextBuffer::apply_delta (which reverses).
    TextDelta to_delta() const {
        TextDelta d;
        d.revision_before = revision_before;
        for (const auto& e : edits) {
            BufferTextEdit be; be.range = e.range; be.replacement = e.replacement;
            d.edits.push_back(std::move(be));
        }
        return d;
    }
    // Reverse delta for undo (requires `original` to be populated).
    TextDelta to_reverse_delta() const {
        TextDelta d;
        d.revision_before = revision_before;
        for (const auto& e : edits) {
            std::size_t rep_len = e.replacement.size();
            BufferTextEdit be; be.range = TextRange<CharOffset>(CharOffset(e.range.start.v), CharOffset(e.range.start.v + rep_len));
            be.replacement = e.original;
            d.edits.push_back(std::move(be));
        }
        return d;
    }

    // Apply forward (text is the cp snapshot). Used by tests.
    std::u32string apply_to(std::u32string_view text) const {
        std::u32string result; std::size_t cursor = 0, len = text.size();
        for (const auto& e : edits) {
            std::size_t s = std::min(e.range.start.v, len);
            std::size_t en = std::min(std::max(e.range.end.v, s), len);
            if (cursor < s) result += text.substr(cursor, s - cursor);
            result += e.replacement;
            cursor = en;
        }
        if (cursor < len) result += text.substr(cursor);
        return result;
    }
};

} // namespace elmd
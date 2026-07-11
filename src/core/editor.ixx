// elmd.core.editor — Editor: ties buffer + selection + undo + command pipeline.
// Faithful port of editor-core::editor, with the deadly `from_text` revision-
// reset bug fixed from the start: undo/redo apply via apply_delta (revision++
// always, never reset). HANDOFF invariant #2/#4/#6.
export module elmd.core.editor;
import std;
import elmd.core.types;
import elmd.core.theme;
import elmd.core.buffer;
import elmd.core.selection;
import elmd.core.command;
import elmd.core.semantic_edit;
import elmd.core.transaction;
import elmd.core.undo;
import elmd.core.input;
import elmd.core.utf;
import elmd.core.dialect;
import elmd.core.document;
import elmd.core.ast;
import elmd.core.document_edit;
import elmd.core.document_position;
import elmd.core.document_projection;
import elmd.core.symbols;
import elmd.core.outline;
import elmd.core.parser;
import elmd.core.serializer;

export namespace elmd {

class Editor {
public:
    Editor() { rebuild_document_full_(); }
    explicit Editor(std::string text, MarkdownDialect dialect = default_dialect()) : dialect_(std::move(dialect)) { buffer_ = TextBuffer::from_text(std::move(text)); rebuild_document_full_(); }

    const TextBuffer& buffer() const { return buffer_; }
    const EditorDocument& document() const { return document_; }
    const DocumentSymbolIndex& symbols() const { return symbols_; }
    const Outline& outline() const { return outline_; }
    std::u32string text_cps() const { return std::u32string(buffer_.text_cps()); }
    std::u32string markdown_cps() const { return serialize_markdown_cps(document_); }
    std::string markdown_utf8() const { return serialize_markdown(document_); }
    Selection selection() const { return selection_; }
    std::uint64_t revision() const { return buffer_.revision(); }
    void set_selection(Selection s) {
        selection_ = s;
        auto anchor = document_position_from_source_offset(document_, s.anchor);
        auto active = document_position_from_source_offset(document_, s.active);
        if (anchor && active) document_selection_ = DocumentSelection{*anchor, *active};
        else document_selection_.reset();
    }
    void set_theme(Theme t) { theme_ = t; }
    Theme theme() const { return theme_; }
    void set_scale_factor(float s) { scale_factor_ = s; }
    float scale_factor() const { return scale_factor_; }
    MarkdownDialect const& dialect() const { return dialect_; }
    void set_dialect(MarkdownDialect dialect) { dialect_ = std::move(dialect); rebuild_document_full_(); }
    bool has_undo() const { return document_history_.has_undo() || undo_.has_undo(); }
    bool has_redo() const { return document_history_.has_redo() || undo_.has_redo(); }
    bool has_document_undo() const { return document_history_.has_undo(); }
    bool has_document_redo() const { return document_history_.has_redo(); }
    std::optional<DocumentSelection> document_selection() const { return document_selection_; }
    void set_document_selection(DocumentSelection selection) {
        document_selection_ = std::move(selection);
        synchronize_legacy_selection_();
    }

    std::optional<DocumentTransaction> execute_document_enter(DocumentSelection selection) {
        auto transaction = document_enter(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_insert_text(DocumentSelection selection, std::u32string_view text) {
        auto transaction = document_insert_text(document_, selection, text);
        if (!transaction) return std::nullopt;
        if (transaction->before.revision == transaction->after.revision) {
            document_selection_ = transaction->selection_after;
            synchronize_legacy_selection_();
        } else {
            apply_document_transaction_(*transaction);
        }
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_delete_backward(DocumentSelection selection) {
        auto transaction = document_delete_backward(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_delete_forward(DocumentSelection selection) {
        auto transaction = document_delete_forward(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_delete_selection(DocumentSelection selection) {
        auto transaction = document_delete_selection(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_indent_list_item(DocumentSelection selection) {
        auto transaction = document_indent_list_item(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_outdent_list_item(DocumentSelection selection) {
        auto transaction = document_outdent_list_item(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_inline_format(DocumentSelection selection, InlineKind kind) {
        auto transaction = document_toggle_inline_format(document_, selection, kind);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_set_heading(DocumentSelection selection, std::uint8_t level) {
        auto transaction = document_set_heading(document_, selection, level);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_block_quote(DocumentSelection selection) {
        auto transaction = document_toggle_block_quote(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_list(DocumentSelection selection, bool ordered, bool task) {
        auto transaction = document_toggle_list(document_, selection, ordered, task);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_task_checkbox(DocumentSelection selection) {
        auto transaction = document_toggle_task_checkbox(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    bool undo_document() {
        auto state = document_history_.undo();
        if (!state) return false;
        document_ = std::move(state->first);
        document_selection_ = state->second;
        refresh_projection_from_document_();
        synchronize_legacy_selection_();
        return true;
    }

    bool redo_document() {
        auto state = document_history_.redo();
        if (!state) return false;
        document_ = std::move(state->first);
        document_selection_ = state->second;
        refresh_projection_from_document_();
        synchronize_legacy_selection_();
        return true;
    }

    // Apply a Command. Returns the transaction if it produced a change.
    std::optional<Transaction> execute_command(const Command& cmd) {
        if (cmd.kind == CommandKind::SetHeading || cmd.kind == CommandKind::ClearHeading
            || cmd.kind == CommandKind::ToggleBlockQuote || cmd.kind == CommandKind::ToggleUnorderedList
            || cmd.kind == CommandKind::ToggleOrderedList || cmd.kind == CommandKind::ToggleTaskList
            || cmd.kind == CommandKind::ToggleTaskCheckbox) {
            if (!document_selection_) return std::nullopt;
            const auto revision_before = buffer_.revision();
            const auto selection_before = selection_;
            bool changed = false;
            if (cmd.kind == CommandKind::SetHeading || cmd.kind == CommandKind::ClearHeading) {
                changed = execute_document_set_heading(*document_selection_,
                    cmd.kind == CommandKind::ClearHeading ? 0 : (std::min)(cmd.level, std::uint8_t{6})).has_value();
            } else if (cmd.kind == CommandKind::ToggleBlockQuote) {
                changed = execute_document_toggle_block_quote(*document_selection_).has_value();
            } else if (cmd.kind == CommandKind::ToggleTaskCheckbox) {
                changed = execute_document_toggle_task_checkbox(*document_selection_).has_value();
            } else {
                changed = execute_document_toggle_list(*document_selection_,
                    cmd.kind == CommandKind::ToggleOrderedList,
                    cmd.kind == CommandKind::ToggleTaskList).has_value();
            }
            if (!changed) return std::nullopt;
            return Transaction(revision_before, selection_before, selection_, TransactionReason::StructuralCommand);
        }
        if (cmd.kind == CommandKind::ToggleStrong || cmd.kind == CommandKind::ToggleEmphasis
            || cmd.kind == CommandKind::ToggleStrikethrough || cmd.kind == CommandKind::ToggleInlineCode
            || cmd.kind == CommandKind::InsertMathInline) {
            if (!document_selection_) return std::nullopt;
            auto kind = InlineKind::Strong;
            if (cmd.kind == CommandKind::ToggleEmphasis) kind = InlineKind::Emphasis;
            else if (cmd.kind == CommandKind::ToggleStrikethrough) kind = InlineKind::Strike;
            else if (cmd.kind == CommandKind::ToggleInlineCode) kind = InlineKind::InlineCode;
            else if (cmd.kind == CommandKind::InsertMathInline) kind = InlineKind::InlineMath;
            const auto revision_before = buffer_.revision();
            const auto selection_before = selection_;
            if (!execute_document_toggle_inline_format(*document_selection_, kind)) return std::nullopt;
            return Transaction(revision_before, selection_before, selection_, TransactionReason::StructuralCommand);
        }
        if (cmd.kind == CommandKind::InsertText) {
            if (!document_selection_) return std::nullopt;
            const auto revision_before = buffer_.revision();
            const auto selection_before = selection_;
            if (!execute_document_insert_text(*document_selection_, cmd.text)) return std::nullopt;
            return Transaction(revision_before, selection_before, selection_, TransactionReason::Typing);
        }
        if ((cmd.kind == CommandKind::IndentListItem || cmd.kind == CommandKind::OutdentListItem) && selection_.is_caret()) {
            auto position = current_document_caret_();
            if (!position) return std::nullopt;
            const auto revision_before = buffer_.revision();
            const auto selection_before = selection_;
            const auto changed = cmd.kind == CommandKind::IndentListItem
                ? execute_document_indent_list_item(DocumentSelection::caret(*position)).has_value()
                : execute_document_outdent_list_item(DocumentSelection::caret(*position)).has_value();
            if (!changed) return std::nullopt;
            return Transaction(revision_before, selection_before, selection_, TransactionReason::StructuralCommand);
        }
        if (cmd.kind == CommandKind::InsertNewline && selection_.is_caret()) {
            auto position = current_document_caret_();
            if (position) {
                const auto revision_before = buffer_.revision();
                const auto selection_before = selection_;
                if (execute_document_enter(DocumentSelection::caret(*position))) {
                    return Transaction(revision_before, selection_before, selection_, TransactionReason::StructuralCommand);
                }
            }
        }
        if (cmd.kind == CommandKind::DeleteBackward || cmd.kind == CommandKind::DeleteForward || cmd.kind == CommandKind::DeleteSelection) {
            if (!selection_.is_caret() && document_selection_) {
                const auto revision_before = buffer_.revision();
                const auto selection_before = selection_;
                const auto changed = execute_document_delete_selection(*document_selection_).has_value();
                if (!changed) return std::nullopt;
                return Transaction(revision_before, selection_before, selection_, TransactionReason::Delete);
            }
            if (selection_.is_caret() && cmd.kind != CommandKind::DeleteSelection) {
                auto position = current_document_caret_();
                if (position) {
                    const auto revision_before = buffer_.revision();
                    const auto selection_before = selection_;
                    const auto changed = cmd.kind == CommandKind::DeleteBackward
                        ? execute_document_delete_backward(DocumentSelection::caret(*position)).has_value()
                        : execute_document_delete_forward(DocumentSelection::caret(*position)).has_value();
                    if (!changed) return std::nullopt;
                    return Transaction(revision_before, selection_before, selection_, TransactionReason::Delete);
                }
            }
        }
        document_history_.clear();
        document_selection_.reset();
        std::u32string text(buffer_.text_cps());
        auto txn = semantic_transaction(cmd, text, document_, selection_, buffer_.revision());
        if (!txn) return std::nullopt;
        // no-op gate
        if (txn->is_empty() &&
            txn->selection_before.anchor == txn->selection_after.anchor &&
            txn->selection_before.active == txn->selection_after.active) {
            return std::nullopt;
        }
        if (!txn->edits.empty()) {
            auto old_text = buffer_.text_utf8();
            auto old_document = document_;
            auto old_symbols = symbols_;
            // populate `original` for reversibility
            for (auto& e : txn->edits) {
                std::size_t s = e.range.start.v, fn = e.range.end.v;
                if (s < fn) e.original = buffer_.text_range(CharRange(CharOffset(s), CharOffset(fn)));
            }
            buffer_.apply_delta(txn->to_delta());
            refresh_document_after_delta_(old_text, old_document, old_symbols, txn->to_delta());
            undo_.push(*txn);
        }
        selection_ = txn->selection_after;
        return txn;
    }

    std::optional<Transaction> undo() {
        if (document_history_.has_undo()) {
            const auto revision_before = buffer_.revision();
            const auto selection_before = selection_;
            if (!undo_document()) return std::nullopt;
            return Transaction(revision_before, selection_before, selection_, TransactionReason::Undo);
        }
        auto txn = undo_.undo();
        if (!txn) return std::nullopt;
        auto old_text = buffer_.text_utf8();
        auto old_document = document_;
        auto old_symbols = symbols_;
        auto rev = txn->to_reverse_delta();
        if (!rev.edits.empty()) {
            buffer_.apply_delta(rev);
            refresh_document_after_delta_(old_text, old_document, old_symbols, rev);
        }
        selection_ = txn->selection_before;
        return txn;
    }
    std::optional<Transaction> redo() {
        if (document_history_.has_redo()) {
            const auto revision_before = buffer_.revision();
            const auto selection_before = selection_;
            if (!redo_document()) return std::nullopt;
            return Transaction(revision_before, selection_before, selection_, TransactionReason::Redo);
        }
        auto txn = undo_.redo();
        if (!txn) return std::nullopt;
        if (!txn->edits.empty()) {
            auto old_text = buffer_.text_utf8();
            auto old_document = document_;
            auto old_symbols = symbols_;
            auto delta = txn->to_delta();
            buffer_.apply_delta(delta);
            refresh_document_after_delta_(old_text, old_document, old_symbols, delta);
        }
        selection_ = txn->selection_after;
        return txn;
    }

    // Translate an input event to a Command (common key bindings). Returns
    // std::nullopt for events that don't synthesise a command (composition,
    // scroll, focus — handled elsewhere).
    std::optional<Command> handle_input(const EditorInputEvent& ev) {
        using K = EditorInputEvent::Kind;
        using TK = KeyCode;
        if (ev.kind == K::TextInput) {
            if (ev.text_input.kind == TextInputEvent::Kind::InsertText && !ev.text_input.text.empty()) {
                return Command::InsertText(ev.text_input.text);
            }
            return std::nullopt;
        }
        if (ev.kind == K::KeyDown) {
            bool ext = ev.key.modifiers.shift;
            switch (ev.key.key_code) {
                case TK::Char:
                    if (ev.key.modifiers.ctrl) {
                        char32_t c = ev.key.ch;
                        if (c == 'b' || c == 'B') { Command c2; c2.kind = CommandKind::ToggleStrong; return c2; }
                        if (c == 'i' || c == 'I') { Command c2; c2.kind = CommandKind::ToggleEmphasis; return c2; }
                        if (c == 'z' || c == 'Z') { Command c2; c2.kind = CommandKind::Undo; return c2; }
                        if (c == 'y' || c == 'Y') { Command c2; c2.kind = CommandKind::Redo; return c2; }
                        if (c == 'c' || c == 'C') { Command c2; c2.kind = CommandKind::Copy; return c2; }
                        if (c == 'x' || c == 'X') { Command c2; c2.kind = CommandKind::Cut; return c2; }
                        if (c == 'v' || c == 'V') { Command c2; c2.kind = CommandKind::Paste; return c2; }
                        if (c == 'a' || c == 'A') { Command c2; c2.kind = CommandKind::SelectAll; return c2; }
                    } else {
                        return Command::InsertText(std::u32string(1, ev.key.ch));
                    }
                    break;
                case TK::Left:  return Command::MoveLeft(ext);
                case TK::Right: return Command::MoveRight(ext);
                case TK::Up:    return Command::MoveUp(ext);
                case TK::Down:  return Command::MoveDown(ext);
                case TK::Home:  { Command c; c.kind = CommandKind::MoveLineStart; c.extend_selection = ext; return c; }
                case TK::End:   { Command c; c.kind = CommandKind::MoveLineEnd;   c.extend_selection = ext; return c; }
                case TK::Backspace: { Command c; c.kind = CommandKind::DeleteBackward; return c; }
                case TK::Delete:    { Command c; c.kind = CommandKind::DeleteForward;  return c; }
                case TK::Enter:     { Command c; c.kind = CommandKind::InsertNewline;  return c; }
                case TK::Tab:       { return Command::InsertText(U"\t"); }
                default: break;
            }
        }
        return std::nullopt;
    }

    // Convenience: mutate caret position (for hit-testing from the UI layer).
    void set_caret(CharOffset p) { set_selection(Selection::caret(p)); }

private:
    TextBuffer buffer_;
    EditorDocument document_;
    DocumentSymbolIndex symbols_;
    Outline outline_;
    Selection selection_;
    UndoManager undo_{1000};
    DocumentHistory document_history_{1000};
    std::optional<DocumentSelection> document_selection_;
    Theme theme_ = Theme::Dark;
    float scale_factor_ = 1.0f;
    MarkdownDialect dialect_ = default_dialect();

    void rebuild_document_full_() {
        auto parsed = parse_text(buffer_.revision(), buffer_.text_utf8(), dialect_);
        document_ = std::move(parsed.document);
        symbols_ = std::move(parsed.symbols);
        outline_ = std::move(parsed.outline);
        if (document_.blocks.empty()) {
            normalize_document(document_);
            auto projection = project_document(document_, dialect_);
            document_.source_map = std::move(projection.source_map);
        }
        auto anchor = document_position_from_source_offset(document_, selection_.anchor);
        auto active = document_position_from_source_offset(document_, selection_.active);
        if (anchor && active) document_selection_ = DocumentSelection{*anchor, *active};
        else document_selection_.reset();
    }

    void refresh_projection_from_document_() {
        auto projection = project_document(document_, dialect_);
        TextDelta delta;
        delta.revision_before = buffer_.revision();
        BufferTextEdit edit;
        edit.range = CharRange(CharOffset(0), CharOffset(buffer_.text_cps().size()));
        edit.replacement = projection.markdown;
        delta.edits.push_back(std::move(edit));
        buffer_.apply_delta(delta);
        document_.revision = buffer_.revision();
        document_.source_map = std::move(projection.source_map);
        document_.metadata = std::move(projection.metadata);
        document_.diagnostics = std::move(projection.diagnostics);
        symbols_ = std::move(projection.symbols);
        outline_ = std::move(projection.outline);
        outline_.revision = document_.revision;
    }

    void synchronize_legacy_selection_() {
        if (!document_selection_) return;
        auto anchor = source_offset_from_document_position(document_, document_selection_->anchor);
        auto active = source_offset_from_document_position(document_, document_selection_->active);
        if (anchor && active) {
            selection_.anchor = *anchor;
            selection_.active = *active;
            selection_.affinity = document_selection_->active.affinity;
        }
    }

    std::optional<DocumentPosition> current_document_caret_() const {
        if (document_selection_ && document_selection_->is_caret()) return document_selection_->active;
        return document_position_from_source_offset(document_, selection_.active);
    }

    void apply_document_transaction_(const DocumentTransaction& transaction) {
        document_history_.push(transaction);
        document_ = transaction.after;
        document_selection_ = transaction.selection_after;
        refresh_projection_from_document_();
        synchronize_legacy_selection_();
    }

    void refresh_document_after_delta_(const std::string& old_text, const EditorDocument& old_document, const DocumentSymbolIndex& old_symbols, const TextDelta& delta) {
        if (delta.edits.size() == 1) {
            IncrementalParseEdit edit;
            edit.old_range = delta.edits[0].range;
            edit.replacement = delta.edits[0].replacement;
            auto parsed = parse_incremental(ParseInput(buffer_.revision(), buffer_.text_utf8(), dialect_), old_document, old_symbols, old_text, edit);
            document_ = std::move(parsed.document);
            symbols_ = std::move(parsed.symbols);
            outline_ = std::move(parsed.outline);
            return;
        }
        rebuild_document_full_();
    }
};

} // namespace elmd

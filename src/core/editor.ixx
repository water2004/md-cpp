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
import elmd.core.document_edit;
import elmd.core.document_position;
import elmd.core.document_projection;
import elmd.core.symbols;
import elmd.core.outline;
import elmd.core.parser;

export namespace elmd {

class Editor {
public:
    Editor() { rebuild_document_full_(); }
    explicit Editor(std::string text, MarkdownDialect dialect = default_dialect()) : dialect_(std::move(dialect)) { buffer_ = TextBuffer::from_text(std::move(text)); rebuild_document_full_(); }

    const TextBuffer& buffer() const { return buffer_; }
    const MarkdownDocument& document() const { return document_; }
    const DocumentSymbolIndex& symbols() const { return symbols_; }
    const Outline& outline() const { return outline_; }
    std::u32string text_cps() const { return std::u32string(buffer_.text_cps()); }
    Selection selection() const { return selection_; }
    std::uint64_t revision() const { return buffer_.revision(); }
    void set_selection(Selection s) { selection_ = s; }
    void set_theme(Theme t) { theme_ = t; }
    Theme theme() const { return theme_; }
    void set_scale_factor(float s) { scale_factor_ = s; }
    float scale_factor() const { return scale_factor_; }
    MarkdownDialect const& dialect() const { return dialect_; }
    void set_dialect(MarkdownDialect dialect) { dialect_ = std::move(dialect); rebuild_document_full_(); }
    bool has_undo() const { return undo_.has_undo(); }
    bool has_redo() const { return undo_.has_redo(); }
    bool has_document_undo() const { return document_history_.has_undo(); }
    bool has_document_redo() const { return document_history_.has_redo(); }
    std::optional<DocumentSelection> document_selection() const { return document_selection_; }
    void set_document_selection(DocumentSelection selection) { document_selection_ = std::move(selection); }

    std::optional<DocumentTransaction> execute_document_enter(DocumentSelection selection) {
        auto transaction = document_enter(document_, selection);
        if (!transaction) return std::nullopt;
        document_history_.push(*transaction);
        document_ = transaction->after;
        document_selection_ = transaction->selection_after;
        refresh_projection_from_document_();
        synchronize_legacy_selection_();
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
    void set_caret(CharOffset p) { selection_ = Selection::caret(p); }

private:
    TextBuffer buffer_;
    MarkdownDocument document_;
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

    void refresh_document_after_delta_(const std::string& old_text, const MarkdownDocument& old_document, const DocumentSymbolIndex& old_symbols, const TextDelta& delta) {
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

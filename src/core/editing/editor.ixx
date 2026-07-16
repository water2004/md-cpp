// elmd.core.editor — owns the authoritative block tree, its single selection,
// reversible operation history, and the command pipeline.
export module elmd.core.editor;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.selection;
import elmd.core.command;
import elmd.core.input;
import elmd.core.utf;
import elmd.core.dialect;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document_edit;
import elmd.core.document_history;
import elmd.core.document_transaction;
import elmd.core.document_symbols;
import elmd.core.text_edit;
import elmd.core.symbols;
import elmd.core.outline;
import elmd.core.parser;
import elmd.core.serializer;

export namespace elmd {

struct EditorDocumentChange {
    std::vector<DocumentTextOperation> text_operations;
    bool structural = false;
    bool forward = true;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
};

inline EditorDocumentChange summarize_document_change(
    const std::vector<DocumentOperation>& operations,
    bool forward,
    std::uint64_t revision_before,
    std::uint64_t revision_after) {
    EditorDocumentChange change;
    change.forward = forward;
    change.revision_before = revision_before;
    change.revision_after = revision_after;
    for (const auto& operation : operations) {
        if (const auto* text = std::get_if<DocumentTextOperation>(&operation)) {
            change.text_operations.push_back(*text);
        } else {
            change.structural = true;
        }
    }
    return change;
}

class Editor {
public:
    Editor() { rebuild_document_full_({}); }
    explicit Editor(std::string text, MarkdownDialect dialect = default_dialect())
        : dialect_(std::move(dialect)) { rebuild_document_full_(std::move(text)); }

    const EditorDocument& document() const { return document_; }
    const DocumentSymbolIndex& symbols() const { return symbols_; }
    const Outline& outline() const { return outline_; }
    std::u32string text_cps() const { return serialize_markdown_cps(document_); }
    std::string text_utf8() const { return serialize_markdown(document_); }
    std::u32string markdown_cps() const { return serialize_markdown_cps(document_); }
    std::string markdown_utf8() const { return serialize_markdown(document_); }
    const TextSelection& selection() const { return selection_; }
    std::optional<std::u32string> editable_source(NodeId id) const {
        return document_editable_text(document_, id);
    }
    std::u32string selected_text_cps() const {
        return document_selected_text(document_, selection_).value_or(std::u32string{});
    }
    std::u32string selected_markdown_cps() const {
        return document_selected_markdown(document_, selection_).value_or(std::u32string{});
    }
    std::uint64_t revision() const { return document_.revision; }
    void set_selection(TextSelection selection) {
        const auto anchor_length = document_edit_detail::editable_length(document_, selection.anchor.container_id);
        const auto active_length = document_edit_detail::editable_length(document_, selection.active.container_id);
        if (!anchor_length || !active_length || selection.anchor.source_offset > *anchor_length
            || selection.active.source_offset > *active_length) throw std::out_of_range("selection is outside editable source");
        selection_ = std::move(selection);
    }
    MarkdownDialect const& dialect() const { return dialect_; }
    void set_dialect(MarkdownDialect dialect) {
        auto markdown = serialize_markdown(document_);
        const auto next_revision = document_.revision + 1;
        dialect_ = std::move(dialect);
        rebuild_document_full_(std::move(markdown), next_revision);
    }
    bool has_undo() const { return document_history_.has_undo(); }
    bool has_redo() const { return document_history_.has_redo(); }
    std::optional<EditorDocumentChange> take_last_document_change() {
        return std::exchange(last_document_change_, std::nullopt);
    }

    std::optional<DocumentTransaction> execute_document_enter(TextSelection selection) {
        auto transaction = document_enter(document_, selection);
        if (!transaction) return std::nullopt;
        if (transaction->revision_before == transaction->revision_after) {
            selection_ = transaction->selection_after;
        } else {
            apply_document_transaction_(*transaction);
        }
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_insert_text(TextSelection selection, std::u32string_view text) {
        auto transaction = document_insert_text(document_, selection, text);
        if (!transaction) return std::nullopt;
        if (transaction->revision_before == transaction->revision_after) {
            selection_ = transaction->selection_after;
        } else {
            apply_document_transaction_(*transaction);
        }
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_paste_text(TextSelection selection, std::u32string_view text) {
        auto transaction = document_paste_text(document_, selection, text);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_insert_soft_break(TextSelection selection) {
        auto transaction = document_insert_soft_break(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    bool execute_document_move(DocumentMove movement, bool extend_selection) {
        auto selection = document_move_selection(document_, selection_, movement, extend_selection);
        if (!selection) return false;
        selection_ = *selection;
        return true;
    }

    std::optional<DocumentTransaction> execute_document_delete_backward(TextSelection selection) {
        auto transaction = document_delete_backward(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_delete_forward(TextSelection selection) {
        auto transaction = document_delete_forward(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_delete_selection(TextSelection selection) {
        auto transaction = document_delete_selection(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_indent_list_item(TextSelection selection) {
        auto transaction = document_indent_list_item(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_outdent_list_item(TextSelection selection) {
        auto transaction = document_outdent_list_item(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_inline_format(TextSelection selection, InlineFormat kind) {
        auto transaction = document_toggle_inline_format(document_, selection, kind);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_set_heading(TextSelection selection, std::uint8_t level) {
        auto transaction = document_set_heading(document_, selection, level);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_block_quote(TextSelection selection) {
        auto transaction = document_toggle_block_quote(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_list(TextSelection selection, bool ordered, bool task) {
        const auto style = task ? ListStyle::Task : ordered ? ListStyle::Ordered : ListStyle::Bullet;
        auto transaction = document_toggle_list(document_, selection, style);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_task_checkbox(TextSelection selection) {
        auto transaction = document_toggle_task_checkbox(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_insert_link(TextSelection selection, const Command& command) {
        auto title = command.title ? std::optional<std::string>{cps_to_utf8(*command.title)} : std::nullopt;
        auto transaction = document_insert_link(document_, selection, cps_to_utf8(command.href), std::move(title));
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_insert_image(TextSelection selection, const Command& command) {
        auto transaction = document_insert_image(document_, selection, cps_to_utf8(command.path), cps_to_utf8(command.alt));
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_insert_atomic_block(TextSelection selection, const Command& command) {
        BlockNode block;
        if (command.kind == CommandKind::InsertCodeBlock) {
            auto language = command.lang ? std::optional<std::string>{cps_to_utf8(*command.lang)} : std::nullopt;
            block = make_code_block(std::move(language));
        } else if (command.kind == CommandKind::InsertMathBlock) {
            block = make_math_block();
        } else if (command.kind == CommandKind::InsertToc) {
            block = make_toc_block();
        } else if (command.kind == CommandKind::InsertTable) {
            block = make_table_block(document_, command.rows, command.cols);
        } else {
            return std::nullopt;
        }
        auto transaction = document_insert_atomic_block(document_, selection, std::move(block));
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_table_edit(
        TextSelection selection,
        DocumentTableEdit edit,
        TableAlignment alignment = TableAlignment::None,
        std::size_t requested_index = 0) {
        auto transaction = document_edit_table(document_, selection, edit, alignment, requested_index);
        if (!transaction) return std::nullopt;
        if (transaction->revision_before == transaction->revision_after) {
            selection_ = transaction->selection_after;
        } else {
            apply_document_transaction_(*transaction);
        }
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_toggle_callout(TextSelection selection, const Command& command) {
        auto transaction = document_toggle_callout(document_, selection, cps_to_utf8(command.callout_kind));
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_insert_footnote(TextSelection selection, const Command&) {
        auto transaction = document_insert_footnote(document_, selection);
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    std::optional<DocumentTransaction> execute_document_create_footnote_definition(
        TextSelection selection,
        std::string label) {
        auto transaction = document_create_footnote_definition(
            document_, selection, std::move(label));
        if (!transaction) return std::nullopt;
        apply_document_transaction_(*transaction);
        return transaction;
    }

    bool undo() {
        last_document_change_.reset();
        const auto* entry = document_history_.next_undo();
        if (!entry) return false;
        auto change = summarize_document_change(
            entry->operations, false, entry->revision_after, entry->revision_before);
        if (!document_history_.undo(document_, selection_)) return false;
        last_document_change_ = std::move(change);
        refresh_derived_from_document_(*last_document_change_);
        return true;
    }

    bool redo() {
        last_document_change_.reset();
        const auto* entry = document_history_.next_redo();
        if (!entry) return false;
        auto change = summarize_document_change(
            entry->operations, true, entry->revision_before, entry->revision_after);
        if (!document_history_.redo(document_, selection_)) return false;
        last_document_change_ = std::move(change);
        refresh_derived_from_document_(*last_document_change_);
        return true;
    }

    bool execute_command(const Command& cmd) {
        last_document_change_.reset();
        if (cmd.kind == CommandKind::Undo) return undo();
        if (cmd.kind == CommandKind::Redo) return redo();
        if (cmd.kind == CommandKind::MoveLeft || cmd.kind == CommandKind::MoveRight
            || cmd.kind == CommandKind::MoveUp || cmd.kind == CommandKind::MoveDown
            || cmd.kind == CommandKind::MoveLineStart || cmd.kind == CommandKind::MoveLineEnd
            || cmd.kind == CommandKind::MoveDocumentStart || cmd.kind == CommandKind::MoveDocumentEnd) {
            auto movement = DocumentMove::Left;
            switch (cmd.kind) {
                case CommandKind::MoveRight: movement = DocumentMove::Right; break;
                case CommandKind::MoveUp: movement = DocumentMove::Up; break;
                case CommandKind::MoveDown: movement = DocumentMove::Down; break;
                case CommandKind::MoveLineStart: movement = DocumentMove::LineStart; break;
                case CommandKind::MoveLineEnd: movement = DocumentMove::LineEnd; break;
                case CommandKind::MoveDocumentStart: movement = DocumentMove::DocumentStart; break;
                case CommandKind::MoveDocumentEnd: movement = DocumentMove::DocumentEnd; break;
                default: break;
            }
            return execute_document_move(movement, cmd.extend_selection);
        }
        if (cmd.kind == CommandKind::SelectAll) {
            auto selection = document_select_all(document_);
            if (!selection) return false;
            selection_ = *selection;
            return true;
        }
        if (cmd.kind == CommandKind::ToggleCallout
            || cmd.kind == CommandKind::InsertFootnote
            || cmd.kind == CommandKind::CreateFootnoteDefinition) {
            const auto changed = cmd.kind == CommandKind::ToggleCallout
                ? execute_document_toggle_callout(selection_, cmd).has_value()
                : cmd.kind == CommandKind::InsertFootnote
                    ? execute_document_insert_footnote(selection_, cmd).has_value()
                    : execute_document_create_footnote_definition(selection_, cmd.footnote_label).has_value();
            return changed;
        }
        if (cmd.kind == CommandKind::MoveTableCellNext || cmd.kind == CommandKind::MoveTableCellPrevious
            || cmd.kind == CommandKind::InsertTableRowAbove || cmd.kind == CommandKind::InsertTableRowBelow
            || cmd.kind == CommandKind::DeleteTableRow || cmd.kind == CommandKind::MoveTableRowUp
            || cmd.kind == CommandKind::MoveTableRowDown || cmd.kind == CommandKind::InsertTableColumnLeft
            || cmd.kind == CommandKind::InsertTableColumnRight || cmd.kind == CommandKind::DeleteTableColumn
            || cmd.kind == CommandKind::MoveTableColumnLeft || cmd.kind == CommandKind::MoveTableColumnRight
            || cmd.kind == CommandKind::SetTableColumnAlignment || cmd.kind == CommandKind::NormalizeTable
            || cmd.kind == CommandKind::InsertTableRowAt || cmd.kind == CommandKind::InsertTableColumnAt
            || cmd.kind == CommandKind::MoveTableRowTo || cmd.kind == CommandKind::MoveTableColumnTo) {
            auto edit = DocumentTableEdit::Normalize;
            switch (cmd.kind) {
                case CommandKind::MoveTableCellNext: edit = DocumentTableEdit::MoveCellNext; break;
                case CommandKind::MoveTableCellPrevious: edit = DocumentTableEdit::MoveCellPrevious; break;
                case CommandKind::InsertTableRowAbove: edit = DocumentTableEdit::InsertRowAbove; break;
                case CommandKind::InsertTableRowBelow: edit = DocumentTableEdit::InsertRowBelow; break;
                case CommandKind::DeleteTableRow: edit = DocumentTableEdit::DeleteRow; break;
                case CommandKind::MoveTableRowUp: edit = DocumentTableEdit::MoveRowUp; break;
                case CommandKind::MoveTableRowDown: edit = DocumentTableEdit::MoveRowDown; break;
                case CommandKind::InsertTableColumnLeft: edit = DocumentTableEdit::InsertColumnLeft; break;
                case CommandKind::InsertTableColumnRight: edit = DocumentTableEdit::InsertColumnRight; break;
                case CommandKind::DeleteTableColumn: edit = DocumentTableEdit::DeleteColumn; break;
                case CommandKind::MoveTableColumnLeft: edit = DocumentTableEdit::MoveColumnLeft; break;
                case CommandKind::MoveTableColumnRight: edit = DocumentTableEdit::MoveColumnRight; break;
                case CommandKind::SetTableColumnAlignment: edit = DocumentTableEdit::SetColumnAlignment; break;
                case CommandKind::InsertTableRowAt: edit = DocumentTableEdit::InsertRowAt; break;
                case CommandKind::InsertTableColumnAt: edit = DocumentTableEdit::InsertColumnAt; break;
                case CommandKind::MoveTableRowTo: edit = DocumentTableEdit::MoveRowTo; break;
                case CommandKind::MoveTableColumnTo: edit = DocumentTableEdit::MoveColumnTo; break;
                default: break;
            }
            return execute_document_table_edit(selection_, edit, cmd.table_alignment, cmd.table_index).has_value();
        }
        if (cmd.kind == CommandKind::InsertCodeBlock || cmd.kind == CommandKind::InsertMathBlock
            || cmd.kind == CommandKind::InsertToc || cmd.kind == CommandKind::InsertTable) {
            return execute_document_insert_atomic_block(selection_, cmd).has_value();
        }
        if (cmd.kind == CommandKind::InsertLink || cmd.kind == CommandKind::InsertImage) {
            const auto changed = cmd.kind == CommandKind::InsertLink
                ? execute_document_insert_link(selection_, cmd).has_value()
                : execute_document_insert_image(selection_, cmd).has_value();
            return changed;
        }
        if (cmd.kind == CommandKind::SetHeading || cmd.kind == CommandKind::ClearHeading
            || cmd.kind == CommandKind::ToggleBlockQuote || cmd.kind == CommandKind::ToggleUnorderedList
            || cmd.kind == CommandKind::ToggleOrderedList || cmd.kind == CommandKind::ToggleTaskList
            || cmd.kind == CommandKind::ToggleTaskCheckbox) {
            bool changed = false;
            if (cmd.kind == CommandKind::SetHeading || cmd.kind == CommandKind::ClearHeading) {
                changed = execute_document_set_heading(selection_,
                    cmd.kind == CommandKind::ClearHeading ? 0 : (std::min)(cmd.level, std::uint8_t{6})).has_value();
            } else if (cmd.kind == CommandKind::ToggleBlockQuote) {
                changed = execute_document_toggle_block_quote(selection_).has_value();
            } else if (cmd.kind == CommandKind::ToggleTaskCheckbox) {
                changed = execute_document_toggle_task_checkbox(selection_).has_value();
            } else {
                changed = execute_document_toggle_list(selection_,
                    cmd.kind == CommandKind::ToggleOrderedList,
                    cmd.kind == CommandKind::ToggleTaskList).has_value();
            }
            return changed;
        }
        if (cmd.kind == CommandKind::ToggleStrong || cmd.kind == CommandKind::ToggleEmphasis
            || cmd.kind == CommandKind::ToggleStrikethrough || cmd.kind == CommandKind::ToggleInlineCode
            || cmd.kind == CommandKind::InsertMathInline) {
            auto kind = InlineFormat::Strong;
            if (cmd.kind == CommandKind::ToggleEmphasis) kind = InlineFormat::Emphasis;
            else if (cmd.kind == CommandKind::ToggleStrikethrough) kind = InlineFormat::Strikethrough;
            else if (cmd.kind == CommandKind::ToggleInlineCode) kind = InlineFormat::Code;
            else if (cmd.kind == CommandKind::InsertMathInline) kind = InlineFormat::Math;
            return execute_document_toggle_inline_format(selection_, kind).has_value();
        }
        if (cmd.kind == CommandKind::InsertText || cmd.kind == CommandKind::Paste) {
            const auto changed = cmd.kind == CommandKind::Paste
                ? execute_document_paste_text(selection_, cmd.text).has_value()
                : execute_document_insert_text(selection_, cmd.text).has_value();
            return changed;
        }
        if (cmd.kind == CommandKind::IndentListItem || cmd.kind == CommandKind::OutdentListItem) {
            const auto changed = cmd.kind == CommandKind::IndentListItem
                ? execute_document_indent_list_item(selection_).has_value()
                : execute_document_outdent_list_item(selection_).has_value();
            return changed;
        }
        if (cmd.kind == CommandKind::InsertNewline || cmd.kind == CommandKind::InsertSoftBreak) {
            const auto changed = cmd.kind == CommandKind::InsertNewline
                ? execute_document_enter(selection_).has_value()
                : execute_document_insert_soft_break(selection_).has_value();
            return changed;
        }
        if (cmd.kind == CommandKind::DeleteBackward || cmd.kind == CommandKind::DeleteForward || cmd.kind == CommandKind::DeleteSelection) {
            bool changed = false;
            if (!selection_.is_caret() || cmd.kind == CommandKind::DeleteSelection) {
                changed = execute_document_delete_selection(selection_).has_value();
            } else if (cmd.kind == CommandKind::DeleteBackward) {
                changed = execute_document_delete_backward(selection_).has_value();
            } else {
                changed = execute_document_delete_forward(selection_).has_value();
            }
            return changed;
        }
        return false;
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

    void set_caret(TextPosition position) { set_selection(TextSelection::caret(position)); }

private:
    EditorDocument document_;
    DocumentSymbolIndex symbols_;
    DocumentSymbolContributions symbol_contributions_;
    Outline outline_;
    DocumentHistory document_history_{1000};
    std::optional<EditorDocumentChange> last_document_change_;
    TextSelection selection_{};
    MarkdownDialect dialect_ = default_dialect();

    void rebuild_document_full_(
        std::string markdown,
        std::optional<std::uint64_t> requested_revision = std::nullopt) {
        const auto revision = requested_revision.value_or(document_.revision);
        auto parsed = parse_text(revision, std::move(markdown), dialect_);
        document_ = std::move(parsed.document);
        symbols_ = std::move(parsed.symbols);
        symbol_contributions_ = std::move(parsed.symbol_contributions);
        outline_ = std::move(parsed.outline);
        document_history_.clear();
        last_document_change_.reset();
        if (document_.root.children.empty()) {
            normalize_document(document_);
        }
        rebuild_document_block_index(document_);
        const auto fragments = document_text_fragments(document_);
        if (!fragments.empty()) {
            selection_ = TextSelection::caret({
                fragments.front().container_id,
                0,
                TextAffinity::Downstream});
        }
    }

    void refresh_derived_from_document_(const EditorDocumentChange& change) {
        if (change.structural) {
            rebuild_document_block_index(document_);
            symbols_ = build_document_symbol_index(document_, &symbol_contributions_);
            outline_ = build_outline_from_blocks(document_.revision, document_.root.children);
            outline_.revision = document_.revision;
            return;
        }

        bool heading_changed = false;
        std::unordered_set<std::uint64_t> refreshed;
        for (const auto& operation : change.text_operations) {
            const auto& edit = change.forward ? operation.forward : operation.inverse;
            if (!refreshed.insert(edit.container_id.v).second) continue;
            if (const auto* block = find_document_block(document_, edit.container_id)) {
                heading_changed = heading_changed || block->kind == BlockKind::Heading;
                update_document_symbol_index(
                    document_, *block, symbol_contributions_, symbols_);
            }
        }
        if (heading_changed) {
            outline_ = build_outline_from_blocks(document_.revision, document_.root.children);
        }
        outline_.revision = document_.revision;
    }

    void apply_document_transaction_(const DocumentTransaction& transaction) {
        document_history_.push(transaction);
        selection_ = transaction.selection_after;
        last_document_change_ = summarize_document_change(
            transaction.operations,
            true,
            transaction.revision_before,
            transaction.revision_after);
        refresh_derived_from_document_(*last_document_change_);
    }

};

} // namespace elmd

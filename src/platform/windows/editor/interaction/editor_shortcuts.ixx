// folia.platform.editor_shortcuts — deterministic configurable shortcut model.
export module folia.platform.editor_shortcuts;
import std;
export import folia.platform.editor_input_command;

export namespace folia::platform::editor {

enum class EditorShortcutScope {
    Global,
    Code,
    Math,
};

enum class EditorShortcutActionKind {
    BuiltIn,
    InsertSnippet,
};

struct EditorShortcutBinding {
    std::string id;
    std::string action_id;
    std::string custom_name;
    EditorShortcutActionKind action_kind = EditorShortcutActionKind::BuiltIn;
    EditorShortcutScope scope = EditorShortcutScope::Global;
    std::optional<EditorKeyGesture> gesture;
    std::u32string snippet;

    bool operator==(EditorShortcutBinding const&) const = default;
};

inline std::optional<std::size_t> resolve_editor_shortcut(
    std::span<EditorShortcutBinding const> bindings,
    EditorKeyGesture gesture,
    EditorShortcutScope active_scope) {
    auto find = [&](EditorShortcutScope scope) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            auto const& binding = bindings[index];
            if (binding.scope == scope && binding.gesture && *binding.gesture == gesture)
                return index;
        }
        return std::nullopt;
    };
    if (active_scope != EditorShortcutScope::Global) {
        if (auto exact = find(active_scope)) return exact;
    }
    return find(EditorShortcutScope::Global);
}

inline std::optional<std::size_t> find_editor_shortcut_conflict(
    std::span<EditorShortcutBinding const> bindings,
    EditorKeyGesture gesture,
    EditorShortcutScope scope,
    std::optional<std::size_t> excluded = std::nullopt) {
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        if (excluded && *excluded == index) continue;
        auto const& binding = bindings[index];
        if (binding.scope == scope && binding.gesture && *binding.gesture == gesture)
            return index;
    }
    return std::nullopt;
}

inline EditorShortcutBinding built_in_shortcut(
    std::string id,
    std::string action_id,
    EditorKeyGesture gesture,
    EditorShortcutScope scope = EditorShortcutScope::Global) {
    return {
        .id = std::move(id),
        .action_id = std::move(action_id),
        .scope = scope,
        .gesture = gesture,
    };
}

inline std::vector<EditorShortcutBinding> default_editor_shortcuts() {
    using K = EditorKey;
    using S = EditorShortcutScope;
    auto ctrl = [](K key, bool shift = false, bool alt = false) {
        return EditorKeyGesture{key, true, shift, alt};
    };
    return {
        built_in_shortcut("file.open", "file.open", ctrl(K::O)),
        built_in_shortcut("file.save", "file.save", ctrl(K::S)),
        built_in_shortcut("search.find", "search.find", ctrl(K::F)),
        built_in_shortcut("search.replace", "search.replace", ctrl(K::H)),
        built_in_shortcut("edit.copy", "edit.copy", ctrl(K::C)),
        built_in_shortcut("edit.cut", "edit.cut", ctrl(K::X)),
        built_in_shortcut("edit.paste", "edit.paste", ctrl(K::V)),
        built_in_shortcut("edit.select_all", "edit.select_all", ctrl(K::A)),
        built_in_shortcut("history.undo", "history.undo", ctrl(K::Z)),
        built_in_shortcut("history.redo_shift", "history.redo", ctrl(K::Z, true)),
        built_in_shortcut("history.redo_y", "history.redo", ctrl(K::Y)),
        built_in_shortcut("format.strong", "format.strong", ctrl(K::B)),
        built_in_shortcut("format.emphasis", "format.emphasis", ctrl(K::I)),
        built_in_shortcut("block.quote", "block.quote", ctrl(K::Q)),
        built_in_shortcut("block.table", "block.table", ctrl(K::T)),
        built_in_shortcut("block.heading1", "block.heading1", ctrl(K::Number1)),
        built_in_shortcut("block.heading2", "block.heading2", ctrl(K::Number2)),
        built_in_shortcut("block.ordered_list", "block.ordered_list", ctrl(K::Number7)),
        built_in_shortcut("block.unordered_list", "block.unordered_list", ctrl(K::Number8)),
        built_in_shortcut("block.task_list", "block.task_list", ctrl(K::Number9)),
        built_in_shortcut("nav.document_start", "nav.document_start", ctrl(K::Home)),
        built_in_shortcut("nav.document_start_select", "nav.document_start_select", ctrl(K::Home, true)),
        built_in_shortcut("nav.document_end", "nav.document_end", ctrl(K::End)),
        built_in_shortcut("nav.document_end_select", "nav.document_end_select", ctrl(K::End, true)),
        built_in_shortcut("table.row_above", "table.row_above", ctrl(K::Up)),
        built_in_shortcut("table.row_below", "table.row_below", ctrl(K::Down)),
        built_in_shortcut("table.column_left", "table.column_left", ctrl(K::Left)),
        built_in_shortcut("table.column_right", "table.column_right", ctrl(K::Right)),
        built_in_shortcut("table.row_move_up", "table.row_move_up", ctrl(K::Up, false, true)),
        built_in_shortcut("table.row_move_down", "table.row_move_down", ctrl(K::Down, false, true)),
        built_in_shortcut("table.column_move_left", "table.column_move_left", ctrl(K::Left, false, true)),
        built_in_shortcut("table.column_move_right", "table.column_move_right", ctrl(K::Right, false, true)),
        built_in_shortcut("table.row_delete", "table.row_delete", ctrl(K::Back)),
        built_in_shortcut("table.column_delete", "table.column_delete", ctrl(K::DeleteKey)),
    };
}

inline EditorInputAction editor_shortcut_input_action(std::string_view action_id) {
    auto execute = [](folia::CommandKind kind) {
        auto command = folia::Command{};
        command.kind = kind;
        return EditorInputAction{EditorInputActionKind::ExecuteCommand, std::move(command)};
    };
    if (action_id == "edit.copy") return {.kind = EditorInputActionKind::Copy};
    if (action_id == "edit.cut") return {.kind = EditorInputActionKind::Cut};
    if (action_id == "edit.paste") return {.kind = EditorInputActionKind::Paste};
    if (action_id == "edit.select_all") return execute(folia::CommandKind::SelectAll);
    if (action_id == "history.undo") return execute(folia::CommandKind::Undo);
    if (action_id == "history.redo") return execute(folia::CommandKind::Redo);
    if (action_id == "format.strong") return execute(folia::CommandKind::ToggleStrong);
    if (action_id == "format.emphasis") return execute(folia::CommandKind::ToggleEmphasis);
    if (action_id == "block.quote") return execute(folia::CommandKind::ToggleBlockQuote);
    if (action_id == "block.ordered_list") return execute(folia::CommandKind::ToggleOrderedList);
    if (action_id == "block.unordered_list") return execute(folia::CommandKind::ToggleUnorderedList);
    if (action_id == "block.task_list") return execute(folia::CommandKind::ToggleTaskList);
    if (action_id == "table.row_above") return execute(folia::CommandKind::InsertTableRowAbove);
    if (action_id == "table.row_below") return execute(folia::CommandKind::InsertTableRowBelow);
    if (action_id == "table.column_left") return execute(folia::CommandKind::InsertTableColumnLeft);
    if (action_id == "table.column_right") return execute(folia::CommandKind::InsertTableColumnRight);
    if (action_id == "table.row_move_up") return execute(folia::CommandKind::MoveTableRowUp);
    if (action_id == "table.row_move_down") return execute(folia::CommandKind::MoveTableRowDown);
    if (action_id == "table.column_move_left") return execute(folia::CommandKind::MoveTableColumnLeft);
    if (action_id == "table.column_move_right") return execute(folia::CommandKind::MoveTableColumnRight);
    if (action_id == "table.row_delete") return execute(folia::CommandKind::DeleteTableRow);
    if (action_id == "table.column_delete") return execute(folia::CommandKind::DeleteTableColumn);

    auto action = EditorInputAction{};
    if (action_id == "block.table") {
        action = execute(folia::CommandKind::InsertTable);
        action.command.rows = 2;
        action.command.cols = 3;
    } else if (action_id == "block.heading1" || action_id == "block.heading2") {
        action = execute(folia::CommandKind::SetHeading);
        action.command.level = action_id.back() == '1' ? 1 : 2;
    } else if (action_id.starts_with("nav.document_")) {
        action = execute(action_id.contains("start")
            ? folia::CommandKind::MoveDocumentStart
            : folia::CommandKind::MoveDocumentEnd);
        action.command.extend_selection = action_id.ends_with("_select");
    }
    return action;
}

} // namespace folia::platform::editor

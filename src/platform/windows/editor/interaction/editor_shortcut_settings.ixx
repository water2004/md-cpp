// folia.platform.editor_shortcut_settings — transactional shortcut-settings model.
export module folia.platform.editor_shortcut_settings;
import std;
export import folia.platform.editor_shortcuts;

export namespace folia::platform::editor {

enum class ShortcutSettingsError {
    None,
    OutOfRange,
    Conflict,
    NotSnippet,
    NotBuiltIn,
    EmptyName,
    EmptyTemplate,
    DuplicateId,
};

struct ShortcutSettingsResult {
    ShortcutSettingsError error = ShortcutSettingsError::None;
    std::optional<std::size_t> conflict_index;

    constexpr explicit operator bool() const {
        return error == ShortcutSettingsError::None;
    }
};

class ShortcutSettingsModel {
public:
    ShortcutSettingsModel() = default;
    explicit ShortcutSettingsModel(std::vector<EditorShortcutBinding> bindings)
        : bindings_(std::move(bindings)) {}

    void Reset(std::vector<EditorShortcutBinding> bindings) {
        bindings_ = std::move(bindings);
    }

    std::vector<EditorShortcutBinding> const& Bindings() const { return bindings_; }

    ShortcutSettingsResult Validate() const {
        for (std::size_t index = 0; index < bindings_.size(); ++index) {
            auto const& binding = bindings_[index];
            if (!binding.gesture) continue;
            if (auto conflict = find_editor_shortcut_conflict(
                bindings_, *binding.gesture, binding.scope, index)) {
                return {ShortcutSettingsError::Conflict, conflict};
            }
        }
        return {};
    }

    ShortcutSettingsResult SetGesture(
        std::size_t index,
        std::optional<EditorKeyGesture> gesture) {
        if (index >= bindings_.size()) return {ShortcutSettingsError::OutOfRange};
        if (gesture) {
            auto const& binding = bindings_[index];
            if (auto conflict = find_editor_shortcut_conflict(
                bindings_, *gesture, binding.scope, index)) {
                return {ShortcutSettingsError::Conflict, conflict};
            }
        }
        bindings_[index].gesture = gesture;
        return {};
    }

    ShortcutSettingsResult SetScope(std::size_t index, EditorShortcutScope scope) {
        if (index >= bindings_.size()) return {ShortcutSettingsError::OutOfRange};
        auto& binding = bindings_[index];
        if (binding.scope == scope) return {};
        if (binding.gesture) {
            if (auto conflict = find_editor_shortcut_conflict(
                bindings_, *binding.gesture, scope, index)) {
                return {ShortcutSettingsError::Conflict, conflict};
            }
        }
        binding.scope = scope;
        return {};
    }

    ShortcutSettingsResult AddSnippet(EditorShortcutBinding binding) {
        if (binding.custom_name.empty()) return {ShortcutSettingsError::EmptyName};
        if (binding.snippet.empty()) return {ShortcutSettingsError::EmptyTemplate};
        if (std::ranges::find(bindings_, binding.id, &EditorShortcutBinding::id)
            != bindings_.end()) {
            return {ShortcutSettingsError::DuplicateId};
        }
        binding.action_kind = EditorShortcutActionKind::InsertSnippet;
        binding.action_id = binding.id;
        if (binding.gesture) {
            if (auto conflict = find_editor_shortcut_conflict(
                bindings_, *binding.gesture, binding.scope)) {
                return {ShortcutSettingsError::Conflict, conflict};
            }
        }
        bindings_.push_back(std::move(binding));
        return {};
    }

    ShortcutSettingsResult UpdateSnippet(
        std::size_t index,
        std::string name,
        std::u32string source,
        EditorShortcutScope scope) {
        if (index >= bindings_.size()) return {ShortcutSettingsError::OutOfRange};
        if (name.empty()) return {ShortcutSettingsError::EmptyName};
        if (source.empty()) return {ShortcutSettingsError::EmptyTemplate};
        auto& binding = bindings_[index];
        if (binding.action_kind != EditorShortcutActionKind::InsertSnippet)
            return {ShortcutSettingsError::NotSnippet};
        if (binding.gesture) {
            if (auto conflict = find_editor_shortcut_conflict(
                bindings_, *binding.gesture, scope, index)) {
                return {ShortcutSettingsError::Conflict, conflict};
            }
        }
        binding.custom_name = std::move(name);
        binding.snippet = std::move(source);
        binding.scope = scope;
        return {};
    }

    ShortcutSettingsResult RemoveSnippet(std::size_t index) {
        if (index >= bindings_.size()) return {ShortcutSettingsError::OutOfRange};
        if (bindings_[index].action_kind != EditorShortcutActionKind::InsertSnippet)
            return {ShortcutSettingsError::NotSnippet};
        bindings_.erase(bindings_.begin() + static_cast<std::ptrdiff_t>(index));
        return {};
    }

    ShortcutSettingsResult ResetBuiltIn(
        std::size_t index,
        EditorShortcutBinding const& default_binding) {
        if (index >= bindings_.size()) return {ShortcutSettingsError::OutOfRange};
        auto& binding = bindings_[index];
        if (binding.action_kind != EditorShortcutActionKind::BuiltIn
            || default_binding.action_kind != EditorShortcutActionKind::BuiltIn
            || binding.id != default_binding.id) {
            return {ShortcutSettingsError::NotBuiltIn};
        }
        if (default_binding.gesture) {
            if (auto conflict = find_editor_shortcut_conflict(
                bindings_, *default_binding.gesture, default_binding.scope, index)) {
                return {ShortcutSettingsError::Conflict, conflict};
            }
        }
        binding.scope = default_binding.scope;
        binding.gesture = default_binding.gesture;
        return {};
    }

private:
    std::vector<EditorShortcutBinding> bindings_;
};

} // namespace folia::platform::editor

#include "support/folia_test.hpp"

import folia.platform.editor_shortcut_settings;

using namespace boost::ut;
using namespace folia::platform::editor;

suite editor_shortcut_settings_tests = [] {

"shortcut settings reject a conflicting gesture without mutating state"_test = [] {
    ShortcutSettingsModel model({
        {.id = "copy", .gesture = EditorKeyGesture{EditorKey::C, true}},
        {.id = "paste", .gesture = EditorKeyGesture{EditorKey::V, true}},
    });
    auto before = model.Bindings();
    auto result = model.SetGesture(1, EditorKeyGesture{EditorKey::C, true});
    expect(!result);
    expect(result.error == ShortcutSettingsError::Conflict);
    expect(result.conflict_index == std::optional<std::size_t>{0});
    expect(model.Bindings() == before);
};

"scope changes are transactional and allow the same gesture in another scope"_test = [] {
    ShortcutSettingsModel model({
        {.id = "global", .scope = EditorShortcutScope::Global,
            .gesture = EditorKeyGesture{EditorKey::B, true}},
        {.id = "code", .scope = EditorShortcutScope::Code,
            .gesture = EditorKeyGesture{EditorKey::B, true}},
        {.id = "math", .scope = EditorShortcutScope::Math,
            .gesture = EditorKeyGesture{EditorKey::M, true}},
    });
    auto conflict = model.SetScope(2, EditorShortcutScope::Global);
    expect(static_cast<bool>(conflict));
    expect(model.Bindings()[2].scope == EditorShortcutScope::Global);
    expect(model.SetGesture(2, EditorKeyGesture{EditorKey::B, true}).error
        == ShortcutSettingsError::Conflict);
    expect(model.Bindings()[2].gesture == std::optional{
        EditorKeyGesture{EditorKey::M, true}});
};

"custom snippets validate fields and can be edited and removed"_test = [] {
    ShortcutSettingsModel model;
    auto empty = EditorShortcutBinding{.id = "snippet.one"};
    expect(model.AddSnippet(empty).error == ShortcutSettingsError::EmptyName);
    empty.custom_name = "Fraction";
    expect(model.AddSnippet(empty).error == ShortcutSettingsError::EmptyTemplate);
    empty.snippet = U"\\frac{$1}{$2}$0";
    expect(static_cast<bool>(model.AddSnippet(empty)));
    expect(model.Bindings().size() == 1_u);
    expect(model.Bindings()[0].action_kind == EditorShortcutActionKind::InsertSnippet);
    expect(static_cast<bool>(model.UpdateSnippet(
        0, "Root", U"\\sqrt{$1}$0", EditorShortcutScope::Math)));
    expect(model.Bindings()[0].custom_name == "Root");
    expect(model.Bindings()[0].scope == EditorShortcutScope::Math);
    expect(static_cast<bool>(model.RemoveSnippet(0)));
    expect(model.Bindings().empty());
};

"built in shortcuts cannot be removed through snippet operations"_test = [] {
    ShortcutSettingsModel model({
        {.id = "copy", .action_id = "edit.copy"},
    });
    expect(model.RemoveSnippet(0).error == ShortcutSettingsError::NotSnippet);
    expect(model.UpdateSnippet(0, "x", U"x", EditorShortcutScope::Global).error
        == ShortcutSettingsError::NotSnippet);
    expect(model.Bindings().size() == 1_u);
};

"built in shortcut reset restores defaults atomically"_test = [] {
    auto defaults = default_editor_shortcuts();
    auto open = std::ranges::find(defaults, std::string{"file.open"},
        &EditorShortcutBinding::id);
    expect(fatal(open != defaults.end()));
    if (open == defaults.end()) return;

    ShortcutSettingsModel model(defaults);
    expect(fatal(bool(model.SetGesture(0, std::nullopt))));
    expect(fatal(bool(model.SetScope(0, EditorShortcutScope::Math))));
    expect(fatal(bool(model.ResetBuiltIn(0, *open))));
    expect(model.Bindings()[0].gesture == open->gesture);
    expect(model.Bindings()[0].scope == open->scope);

    auto conflicting = defaults;
    conflicting[1].gesture = open->gesture;
    ShortcutSettingsModel conflictModel(std::move(conflicting));
    expect(fatal(bool(conflictModel.SetGesture(0, std::nullopt))));
    auto before = conflictModel.Bindings()[0];
    auto result = conflictModel.ResetBuiltIn(0, *open);
    expect(result.error == ShortcutSettingsError::Conflict);
    expect(conflictModel.Bindings()[0] == before);
};

};

#include "support/folia_test.hpp"

import folia.platform.editor_shortcuts;

using namespace boost::ut;
using namespace folia;
using namespace folia::platform::editor;

suite editor_shortcut_tests = [] {

"unset shortcut is disabled and exact scope overrides global"_test = [] {
    auto bindings = std::vector<EditorShortcutBinding>{
        {.id = "disabled", .action_id = "edit.copy"},
        {.id = "global", .action_id = "edit.copy", .gesture = EditorKeyGesture{EditorKey::B, true}},
        {.id = "math", .action_id = "custom", .scope = EditorShortcutScope::Math,
            .gesture = EditorKeyGesture{EditorKey::B, true}},
    };
    auto gesture = EditorKeyGesture{EditorKey::B, true};
    auto global = resolve_editor_shortcut(bindings, gesture, EditorShortcutScope::Global);
    auto code = resolve_editor_shortcut(bindings, gesture, EditorShortcutScope::Code);
    auto math = resolve_editor_shortcut(bindings, gesture, EditorShortcutScope::Math);
    expect(fatal(global.has_value()));
    expect(fatal(code.has_value()));
    expect(fatal(math.has_value()));
    expect(*global == 1_u);
    expect(*code == 1_u);
    expect(*math == 2_u);
    expect(!resolve_editor_shortcut(bindings, {EditorKey::C, true}, EditorShortcutScope::Global));
};

"conflicts are reported only inside the same scope"_test = [] {
    auto bindings = std::vector<EditorShortcutBinding>{
        {.id = "global", .gesture = EditorKeyGesture{EditorKey::B, true}},
        {.id = "code", .scope = EditorShortcutScope::Code,
            .gesture = EditorKeyGesture{EditorKey::B, true}},
    };
    auto gesture = EditorKeyGesture{EditorKey::B, true};
    auto global = find_editor_shortcut_conflict(
        bindings, gesture, EditorShortcutScope::Global);
    auto code = find_editor_shortcut_conflict(
        bindings, gesture, EditorShortcutScope::Code);
    expect(fatal(global.has_value()));
    expect(fatal(code.has_value()));
    expect(*global == 0_u);
    expect(*code == 1_u);
    expect(!find_editor_shortcut_conflict(bindings, gesture, EditorShortcutScope::Math));
    expect(!find_editor_shortcut_conflict(
        bindings, gesture, EditorShortcutScope::Global, std::size_t{0}));
};

"default shortcut catalog maps built in actions without legacy translation"_test = [] {
    auto bindings = default_editor_shortcuts();
    auto copy = resolve_editor_shortcut(
        bindings, {EditorKey::C, true}, EditorShortcutScope::Global);
    expect(fatal(copy.has_value()));
    expect(editor_shortcut_input_action(bindings[*copy].action_id).kind
        == EditorInputActionKind::Copy);
    expect(editor_shortcut_input_action("block.heading2").command.level == 2_u);
    expect(!TranslateEditorKeyGesture({EditorKey::C, true}).Handled());
};

};

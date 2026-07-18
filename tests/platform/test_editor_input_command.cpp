#include "support/folia_test.hpp"

import folia.platform.editor_input_command;

using namespace boost::ut;
using namespace folia;
using namespace folia::platform::editor;

suite editor_input_command_tests = [] {

"text navigation gestures preserve selection extension"_test = [] {
    auto left = TranslateEditorKeyGesture({
        .key = EditorKey::Left,
        .shift = true,
    });
    expect(left.kind == EditorInputActionKind::ExecuteCommand);
    expect(left.command.kind == CommandKind::MoveLeft);
    expect(left.command.extend_selection);

    auto up = TranslateEditorKeyGesture({
        .key = EditorKey::Up,
        .shift = true,
    });
    expect(up.kind == EditorInputActionKind::VisualLineUp);
    expect(up.command.extend_selection);
};

"enter and tab remain semantic structure actions"_test = [] {
    auto enter = TranslateEditorKeyGesture({.key = EditorKey::Enter});
    expect(enter.kind == EditorInputActionKind::ExecuteCommandIfApplied);
    expect(enter.command.kind == CommandKind::InsertNewline);

    auto softBreak = TranslateEditorKeyGesture({
        .key = EditorKey::Enter,
        .shift = true,
    });
    expect(softBreak.command.kind == CommandKind::InsertSoftBreak);
    expect(TranslateEditorKeyGesture({.key = EditorKey::Tab}).kind
        == EditorInputActionKind::TabForward);
    expect(TranslateEditorKeyGesture({.key = EditorKey::Tab, .shift = true}).kind
        == EditorInputActionKind::TabBackward);
};

"control gestures distinguish formatting clipboard and table structure"_test = [] {
    auto bold = TranslateEditorKeyGesture({
        .key = EditorKey::B,
        .control = true,
    });
    expect(bold.command.kind == CommandKind::ToggleStrong);
    expect(TranslateEditorKeyGesture({.key = EditorKey::C, .control = true}).kind
        == EditorInputActionKind::Copy);

    auto insertRow = TranslateEditorKeyGesture({
        .key = EditorKey::Up,
        .control = true,
    });
    expect(insertRow.command.kind == CommandKind::InsertTableRowAbove);
    auto moveRow = TranslateEditorKeyGesture({
        .key = EditorKey::Up,
        .control = true,
        .alt = true,
    });
    expect(moveRow.command.kind == CommandKind::MoveTableRowUp);
};

"unknown and control-modified unsupported keys are not consumed"_test = [] {
    expect(!TranslateEditorKeyGesture({
        .key = static_cast<EditorKey>(0xffffu),
    }).Handled());
    expect(!TranslateEditorKeyGesture({
        .key = EditorKey::Enter,
        .control = true,
    }).Handled());
};

}; // suite editor_input_command_tests

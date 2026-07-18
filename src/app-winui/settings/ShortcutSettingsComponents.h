#pragma once

import folia.platform.editor_shortcuts;

namespace winrt::Folia::settings_ui
{
    enum class ShortcutCaptureDisposition
    {
        Ignore,
        Cancel,
        Accept,
    };

    struct ShortcutCaptureResult
    {
        ShortcutCaptureDisposition disposition = ShortcutCaptureDisposition::Ignore;
        std::optional<folia::platform::editor::EditorKeyGesture> gesture;
    };

    std::int32_t ShortcutScopeIndex(
        folia::platform::editor::EditorShortcutScope scope);
    folia::platform::editor::EditorShortcutScope ShortcutScopeFromIndex(
        std::int32_t index);
    winrt::hstring ShortcutActionLabel(
        folia::platform::editor::EditorShortcutBinding const& binding);
    winrt::hstring ShortcutGestureLabel(
        std::optional<folia::platform::editor::EditorKeyGesture> const& gesture);
    ShortcutCaptureResult CaptureShortcutGesture(
        winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
}

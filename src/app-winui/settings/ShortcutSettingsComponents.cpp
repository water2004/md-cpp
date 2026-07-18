#include "pch.h"
#include "settings/ShortcutSettingsComponents.h"
#include "localization/Localization.h"

namespace
{
    using EditorKey = folia::platform::editor::EditorKey;

    winrt::hstring KeyLabel(EditorKey key)
    {
        auto value = static_cast<std::uint32_t>(key);
        if (value >= static_cast<std::uint32_t>(EditorKey::A)
            && value <= static_cast<std::uint32_t>(EditorKey::Z))
            return winrt::hstring(std::wstring(1, static_cast<wchar_t>(value)));
        if (value >= static_cast<std::uint32_t>(EditorKey::Number0)
            && value <= static_cast<std::uint32_t>(EditorKey::Number9))
            return winrt::hstring(std::wstring(1, static_cast<wchar_t>(value)));
        if (value >= static_cast<std::uint32_t>(EditorKey::F1)
            && value <= static_cast<std::uint32_t>(EditorKey::F12))
            return L"F" + winrt::to_hstring(
                value - static_cast<std::uint32_t>(EditorKey::F1) + 1);
        switch (key)
        {
            case EditorKey::Back: return L"Backspace";
            case EditorKey::Tab: return L"Tab";
            case EditorKey::Enter: return L"Enter";
            case EditorKey::Escape: return L"Esc";
            case EditorKey::Space: return L"Space";
            case EditorKey::PageUp: return L"Page Up";
            case EditorKey::PageDown: return L"Page Down";
            case EditorKey::End: return L"End";
            case EditorKey::Home: return L"Home";
            case EditorKey::Left: return L"Left";
            case EditorKey::Up: return L"Up";
            case EditorKey::Right: return L"Right";
            case EditorKey::Down: return L"Down";
            case EditorKey::DeleteKey: return L"Delete";
            default: return winrt::to_hstring(value);
        }
    }

    bool ModifierDown(winrt::Windows::System::VirtualKey key)
    {
        auto state = winrt::Microsoft::UI::Input::InputKeyboardSource::
            GetKeyStateForCurrentThread(key);
        return (static_cast<std::uint32_t>(state) & 0x1u) != 0;
    }

    bool IsModifierKey(winrt::Windows::System::VirtualKey key)
    {
        using winrt::Windows::System::VirtualKey;
        return key == VirtualKey::Control || key == VirtualKey::LeftControl
            || key == VirtualKey::RightControl || key == VirtualKey::Shift
            || key == VirtualKey::LeftShift || key == VirtualKey::RightShift
            || key == VirtualKey::Menu || key == VirtualKey::LeftMenu
            || key == VirtualKey::RightMenu;
    }
}

namespace winrt::Folia::settings_ui
{
    using ShortcutBinding = folia::platform::editor::EditorShortcutBinding;
    using ShortcutScope = folia::platform::editor::EditorShortcutScope;
    using EditorKey = folia::platform::editor::EditorKey;
    using EditorKeyGesture = folia::platform::editor::EditorKeyGesture;

    std::int32_t ShortcutScopeIndex(ShortcutScope scope)
    {
        return static_cast<std::int32_t>(scope);
    }

    ShortcutScope ShortcutScopeFromIndex(std::int32_t index)
    {
        if (index == 1) return ShortcutScope::Code;
        if (index == 2) return ShortcutScope::Math;
        return ShortcutScope::Global;
    }

    winrt::hstring ShortcutActionLabel(ShortcutBinding const& binding)
    {
        if (!binding.custom_name.empty()) return winrt::to_hstring(binding.custom_name);
        constexpr std::array<std::pair<std::string_view, std::wstring_view>, 48> labels{{
            {"file.open", L"Open"}, {"file.save", L"Save"},
            {"file.save_as", L"ShortcutSaveAs"}, {"file.export_pdf", L"Pdf"},
            {"search.find", L"Find"}, {"search.replace", L"Replace"},
            {"edit.copy", L"Copy"}, {"edit.cut", L"Cut"},
            {"edit.paste", L"Paste"}, {"edit.select_all", L"SelectAll"},
            {"history.undo", L"ShortcutUndo"}, {"history.redo", L"ShortcutRedo"},
            {"format.strong", L"Bold"}, {"format.emphasis", L"Italic"},
            {"format.strikethrough", L"Strikethrough"},
            {"format.inline_code", L"InlineCode"},
            {"block.quote", L"Quote"}, {"block.table", L"Table"},
            {"block.code", L"CodeBlock"}, {"math.inline", L"InlineMath"},
            {"math.block", L"MathBlock"}, {"insert.link", L"Link"},
            {"insert.image", L"Image"}, {"insert.footnote", L"Footnote"},
            {"insert.toc", L"TableOfContents"}, {"callout.note", L"Note"},
            {"callout.tip", L"Tip"}, {"callout.warning", L"Warning"},
            {"view.source_mode", L"SourceMode"},
            {"block.heading1", L"Heading1"}, {"block.heading2", L"Heading2"},
            {"block.ordered_list", L"NumberedList"},
            {"block.unordered_list", L"BulletedList"},
            {"block.task_list", L"TaskList"},
            {"nav.document_start", L"ShortcutDocumentStart"},
            {"nav.document_start_select", L"ShortcutSelectDocumentStart"},
            {"nav.document_end", L"ShortcutDocumentEnd"},
            {"nav.document_end_select", L"ShortcutSelectDocumentEnd"},
            {"table.row_above", L"ShortcutTableRowAbove"},
            {"table.row_below", L"ShortcutTableRowBelow"},
            {"table.column_left", L"ShortcutTableColumnLeft"},
            {"table.column_right", L"ShortcutTableColumnRight"},
            {"table.row_move_up", L"ShortcutTableMoveRowUp"},
            {"table.row_move_down", L"ShortcutTableMoveRowDown"},
            {"table.column_move_left", L"ShortcutTableMoveColumnLeft"},
            {"table.column_move_right", L"ShortcutTableMoveColumnRight"},
            {"table.row_delete", L"ShortcutTableDeleteRow"},
            {"table.column_delete", L"ShortcutTableDeleteColumn"},
        }};
        for (auto const& [action, resource] : labels)
            if (binding.action_id == action) return Localize(resource);
        return winrt::to_hstring(binding.action_id);
    }

    winrt::hstring ShortcutGestureLabel(
        std::optional<EditorKeyGesture> const& gesture)
    {
        if (!gesture) return Localize(L"ShortcutUnassigned");
        winrt::hstring result;
        if (gesture->control) result = L"Ctrl+";
        if (gesture->shift) result = result + L"Shift+";
        if (gesture->alt) result = result + L"Alt+";
        return result + KeyLabel(gesture->key);
    }

    ShortcutCaptureResult CaptureShortcutGesture(
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        auto key = args.Key();
        if (key == Windows::System::VirtualKey::Escape)
            return {.disposition = ShortcutCaptureDisposition::Cancel};
        if (IsModifierKey(key)) return {};

        auto gesture = EditorKeyGesture{
            .key = static_cast<EditorKey>(static_cast<std::uint32_t>(key)),
            .control = ModifierDown(Windows::System::VirtualKey::Control)
                || ModifierDown(Windows::System::VirtualKey::LeftControl)
                || ModifierDown(Windows::System::VirtualKey::RightControl),
            .shift = ModifierDown(Windows::System::VirtualKey::Shift)
                || ModifierDown(Windows::System::VirtualKey::LeftShift)
                || ModifierDown(Windows::System::VirtualKey::RightShift),
            .alt = ModifierDown(Windows::System::VirtualKey::Menu)
                || ModifierDown(Windows::System::VirtualKey::LeftMenu)
                || ModifierDown(Windows::System::VirtualKey::RightMenu),
        };
        if ((key == Windows::System::VirtualKey::Back
                || key == Windows::System::VirtualKey::Delete)
            && !gesture.control && !gesture.shift && !gesture.alt)
            return {.disposition = ShortcutCaptureDisposition::Accept};
        return {
            .disposition = ShortcutCaptureDisposition::Accept,
            .gesture = gesture,
        };
    }
}

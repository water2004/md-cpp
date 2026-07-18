#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

import folia.core.utf;
import folia.core.snippet_template;

namespace
{
    namespace Xaml = winrt::Microsoft::UI::Xaml;
    namespace Controls = winrt::Microsoft::UI::Xaml::Controls;
    using ShortcutBinding = folia::platform::editor::EditorShortcutBinding;
    using ShortcutScope = folia::platform::editor::EditorShortcutScope;
    using EditorKey = folia::platform::editor::EditorKey;
    using EditorKeyGesture = folia::platform::editor::EditorKeyGesture;

    std::int32_t ScopeIndex(ShortcutScope scope)
    {
        return static_cast<std::int32_t>(scope);
    }

    ShortcutScope ScopeFromIndex(std::int32_t index)
    {
        if (index == 1) return ShortcutScope::Code;
        if (index == 2) return ShortcutScope::Math;
        return ShortcutScope::Global;
    }

    winrt::hstring ActionLabel(ShortcutBinding const& binding)
    {
        if (!binding.custom_name.empty()) return winrt::to_hstring(binding.custom_name);
        constexpr std::array<std::pair<std::string_view, std::wstring_view>, 49> Labels{{
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
            {"", L""},
        }};
        for (auto const& [action, resource] : Labels)
            if (binding.action_id == action) return winrt::Folia::Localize(resource);
        return winrt::to_hstring(binding.action_id);
    }

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

    winrt::hstring GestureLabel(std::optional<EditorKeyGesture> const& gesture)
    {
        if (!gesture) return winrt::Folia::Localize(L"ShortcutUnassigned");
        winrt::hstring result;
        if (gesture->control) result = L"Ctrl+";
        if (gesture->shift) result = result + L"Shift+";
        if (gesture->alt) result = result + L"Alt+";
        return result + KeyLabel(gesture->key);
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

namespace winrt::Folia
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using settings_ui::Brush;
    using settings_ui::PageHeading;
    using settings_ui::Text;

    UIElement SettingsView::BuildShortcutsPage()
    {
        StackPanel page = settings_ui::PagePanel();

        auto heading = PageHeading(Localize(L"Shortcuts"));
        page.Children().Append(heading);
        auto description = Text(Localize(L"ShortcutsDescription"));
        page.Children().Append(description);

        Grid listHeader;
        listHeader.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition addColumn;
        addColumn.Width(GridLengthHelper::Auto());
        listHeader.ColumnDefinitions().Append(addColumn);
        auto listTitle = Text(Localize(L"ShortcutBindings"), 18);
        listTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        listTitle.VerticalAlignment(VerticalAlignment::Center);
        listHeader.Children().Append(listTitle);
        Button addAction;
        addAction.Content(box_value(Localize(L"AddInsertionAction")));
        addAction.Click([this](auto const&, auto const&)
        {
            ShowSnippetEditorAsync(std::nullopt);
        });
        Grid::SetColumn(addAction, 1);
        listHeader.Children().Append(addAction);
        page.Children().Append(listHeader);

        Grid tableHeader;
        tableHeader.ColumnSpacing(12);
        for (auto width : {2.0, 1.0, 1.2})
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
            tableHeader.ColumnDefinitions().Append(column);
        }
        ColumnDefinition operationsColumn;
        operationsColumn.Width(GridLengthHelper::Auto());
        tableHeader.ColumnDefinitions().Append(operationsColumn);
        auto appendHeader = [&](hstring const& label, int column)
        {
            auto text = Text(label, 12);
            text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            Grid::SetColumn(text, column);
            tableHeader.Children().Append(text);
        };
        appendHeader(Localize(L"ShortcutAction"), 0);
        appendHeader(Localize(L"ShortcutScope"), 1);
        appendHeader(Localize(L"ShortcutKeys"), 2);
        appendHeader(Localize(L"ShortcutOperations"), 3);
        page.Children().Append(tableHeader);

        shortcutList_.Spacing(0);
        page.Children().Append(shortcutList_);

        shortcutStatus_.TextWrapping(TextWrapping::Wrap);
        page.Children().Append(shortcutStatus_);
        RefreshShortcutList();

        ScrollViewer scroll;
        scroll.HorizontalScrollMode(ScrollMode::Disabled);
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroll.VerticalScrollMode(ScrollMode::Enabled);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.Content(page);
        return scroll;
    }

    void SettingsView::SetShortcutStatus(hstring const& message, bool error)
    {
        shortcutStatus_.Text(message);
        shortcutStatus_.Foreground(error ? Brush({196, 43, 28, 255}) : nullptr);
    }

    void SettingsView::RefreshShortcutList()
    {
        refreshing_ = true;
        struct ResetRefreshing { bool& value; ~ResetRefreshing() { value = false; } } reset{refreshing_};
        capturingShortcut_.reset();
        shortcutButtons_.clear();
        shortcutList_.Children().Clear();
        auto const& bindings = shortcutModel_.Bindings();
        shortcutButtons_.reserve(bindings.size());

        for (std::size_t index = 0; index < bindings.size(); ++index)
        {
            auto const& binding = bindings[index];
            Grid row;
            row.Padding(Thickness{4, 4, 4, 4});
            row.ColumnSpacing(12);
            for (auto width : {2.0, 1.0, 1.2})
            {
                ColumnDefinition column;
                column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
                row.ColumnDefinitions().Append(column);
            }
            ColumnDefinition operationsColumn;
            operationsColumn.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(operationsColumn);

            auto action = Text(ActionLabel(binding));
            action.VerticalAlignment(VerticalAlignment::Center);
            row.Children().Append(action);

            ComboBox scope;
            scope.Items().Append(box_value(Localize(L"ShortcutScopeGlobal")));
            scope.Items().Append(box_value(Localize(L"ShortcutScopeCode")));
            scope.Items().Append(box_value(Localize(L"ShortcutScopeMath")));
            scope.SelectedIndex(ScopeIndex(binding.scope));
            scope.SelectionChanged([this, index, scope](auto const&, auto const&)
            {
                if (!refreshing_) ChangeShortcutScope(index, scope.SelectedIndex());
            });
            Grid::SetColumn(scope, 1);
            row.Children().Append(scope);

            Button capture;
            capture.HorizontalAlignment(HorizontalAlignment::Stretch);
            capture.HorizontalContentAlignment(HorizontalAlignment::Center);
            capture.Content(box_value(GestureLabel(binding.gesture)));
            capture.Click([this, index](auto const&, auto const&) { BeginShortcutCapture(index); });
            capture.KeyDown([this, index](
                auto const&, Input::KeyRoutedEventArgs const& args)
            {
                CaptureShortcut(index, args);
            });
            Grid::SetColumn(capture, 2);
            row.Children().Append(capture);
            shortcutButtons_.push_back(capture);

            Button more;
            FontIcon moreIcon;
            moreIcon.Glyph(L"\xE712");
            more.Content(moreIcon);
            more.Padding(Thickness{8, 4, 8, 4});
            more.HorizontalAlignment(HorizontalAlignment::Right);
            more.VerticalAlignment(VerticalAlignment::Center);
            ToolTipService::SetToolTip(more, box_value(Localize(L"MoreActions")));
            MenuFlyout menu;
            MenuFlyoutItem clear;
            clear.Text(Localize(L"Clear"));
            clear.Click([this, index](auto const&, auto const&) { ClearShortcut(index); });
            menu.Items().Append(clear);
            if (binding.action_kind
                == folia::platform::editor::EditorShortcutActionKind::InsertSnippet)
            {
                MenuFlyoutItem edit;
                edit.Text(Localize(L"Edit"));
                edit.Click([this, index](auto const&, auto const&)
                {
                    ShowSnippetEditorAsync(index);
                });
                menu.Items().Append(edit);
                MenuFlyoutItem remove;
                remove.Text(Localize(L"Remove"));
                remove.Click([this, index](auto const&, auto const&) { RemoveSnippet(index); });
                menu.Items().Append(remove);
            }
            else
            {
                MenuFlyoutItem resetDefault;
                resetDefault.Text(Localize(L"ResetDefault"));
                resetDefault.Click([this, index](auto const&, auto const&) { ResetShortcut(index); });
                menu.Items().Append(resetDefault);
            }
            more.Flyout(menu);
            Grid::SetColumn(more, 3);
            row.Children().Append(more);

            Border separator;
            separator.BorderThickness(Thickness{0, 0, 0, 1});
            separator.BorderBrush(Brush({128, 128, 128, 40}));
            separator.Child(row);
            shortcutList_.Children().Append(separator);
        }
    }

    void SettingsView::BeginShortcutCapture(std::size_t index)
    {
        auto const& bindings = shortcutModel_.Bindings();
        if (index >= shortcutButtons_.size() || index >= bindings.size()) return;
        if (capturingShortcut_ && *capturingShortcut_ < shortcutButtons_.size())
            shortcutButtons_[*capturingShortcut_].Content(box_value(
                GestureLabel(bindings[*capturingShortcut_].gesture)));
        capturingShortcut_ = index;
        shortcutButtons_[index].Content(box_value(Localize(L"PressShortcut")));
        shortcutButtons_[index].Focus(FocusState::Programmatic);
        SetShortcutStatus(Localize(L"ShortcutCaptureHint"));
    }

    void SettingsView::CaptureShortcut(
        std::size_t index,
        Input::KeyRoutedEventArgs const& args)
    {
        auto const& bindings = shortcutModel_.Bindings();
        if (!capturingShortcut_ || *capturingShortcut_ != index || index >= bindings.size())
            return;
        auto key = args.Key();
        if (key == Windows::System::VirtualKey::Escape)
        {
            capturingShortcut_.reset();
            shortcutButtons_[index].Content(box_value(GestureLabel(bindings[index].gesture)));
            SetShortcutStatus({});
            args.Handled(true);
            return;
        }
        if (IsModifierKey(key))
        {
            args.Handled(true);
            return;
        }
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
        {
            ClearShortcut(index);
            args.Handled(true);
            return;
        }
        auto result = shortcutModel_.SetGesture(index, gesture);
        if (HandleShortcutResult(result, gesture))
        {
            capturingShortcut_.reset();
            if (ApplyShortcutSettings()) RefreshShortcutList();
        }
        args.Handled(true);
    }

    void SettingsView::ClearShortcut(std::size_t index)
    {
        auto result = shortcutModel_.SetGesture(index, std::nullopt);
        if (!HandleShortcutResult(result)) return;
        capturingShortcut_.reset();
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }

    void SettingsView::ResetShortcut(std::size_t index)
    {
        auto const& bindings = shortcutModel_.Bindings();
        if (index >= bindings.size()) return;
        auto defaults = folia::platform::editor::default_editor_shortcuts();
        auto found = std::ranges::find(
            defaults, bindings[index].id, &ShortcutBinding::id);
        if (found == defaults.end()) return;
        auto result = shortcutModel_.ResetBuiltIn(index, *found);
        if (!HandleShortcutResult(result, found->gesture)) return;
        capturingShortcut_.reset();
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }

    void SettingsView::ChangeShortcutScope(std::size_t index, std::int32_t selectedIndex)
    {
        auto const& bindings = shortcutModel_.Bindings();
        if (index >= bindings.size()) return;
        auto gesture = bindings[index].gesture;
        auto result = shortcutModel_.SetScope(index, ScopeFromIndex(selectedIndex));
        if (!HandleShortcutResult(result, gesture))
        {
            RefreshShortcutList();
            return;
        }
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }

    bool SettingsView::HandleShortcutResult(
        folia::platform::editor::ShortcutSettingsResult const& result,
        std::optional<EditorKeyGesture> gesture)
    {
        using folia::platform::editor::ShortcutSettingsError;
        if (result) return true;
        if (result.error == ShortcutSettingsError::Conflict && result.conflict_index)
        {
            auto const& bindings = shortcutModel_.Bindings();
            if (*result.conflict_index < bindings.size())
            {
                if (!gesture) gesture = bindings[*result.conflict_index].gesture;
                SetShortcutStatus(LocalizeFormat(
                    L"ShortcutConflict",
                    {GestureLabel(gesture), ActionLabel(bindings[*result.conflict_index])}),
                    true);
            }
        }
        else if (result.error == ShortcutSettingsError::EmptyName
            || result.error == ShortcutSettingsError::EmptyTemplate)
        {
            SetShortcutStatus(Localize(L"SnippetFieldsRequired"), true);
        }
        return false;
    }

    bool SettingsView::ApplyShortcutSettings()
    {
        if (detached_ || !applySettings_) return false;
        auto validation = shortcutModel_.Validate();
        if (!HandleShortcutResult(validation)) return false;
        auto proposed = appliedSettings_;
        proposed.shortcutBindings = shortcutModel_.Bindings();
        if (auto error = applySettings_(proposed))
        {
            shortcutModel_.Reset(appliedSettings_.shortcutBindings);
            settings_.shortcutBindings = appliedSettings_.shortcutBindings;
            SetShortcutStatus(*error, true);
            RefreshShortcutList();
            return false;
        }
        settings_.shortcutBindings = proposed.shortcutBindings;
        appliedSettings_.shortcutBindings = proposed.shortcutBindings;
        SetShortcutStatus(Localize(L"ShortcutsSaved"));
        return true;
    }

    fire_and_forget SettingsView::ShowSnippetEditorAsync(
        std::optional<std::size_t> index)
    {
        auto lifetime = shared_from_this();
        if (editorDialogOpen_) co_return;
        std::optional<ShortcutBinding> existing;
        if (index)
        {
            auto const& bindings = shortcutModel_.Bindings();
            if (*index >= bindings.size()
                || bindings[*index].action_kind
                    != folia::platform::editor::EditorShortcutActionKind::InsertSnippet)
                co_return;
            existing = bindings[*index];
        }

        editorDialogOpen_ = true;
        struct DialogGuard
        {
            bool& open;
            ~DialogGuard() { open = false; }
        } guard{editorDialogOpen_};

        ContentDialog dialog;
        dialog.XamlRoot(navigation_.XamlRoot());
        dialog.Title(box_value(Localize(existing
            ? L"EditInsertionAction" : L"CustomInsertionAction")));
        dialog.PrimaryButtonText(Localize(existing
            ? L"SaveInsertionAction" : L"AddInsertionAction"));
        dialog.CloseButtonText(Localize(L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel fields;
        fields.Spacing(10);
        fields.MinWidth(560);
        fields.Children().Append(Text(Localize(L"SnippetPlaceholderDescription"), 12));
        TextBox name;
        name.Header(box_value(Localize(L"ActionName")));
        name.PlaceholderText(Localize(L"ActionNameExample"));
        if (existing) name.Text(winrt::to_hstring(existing->custom_name));
        fields.Children().Append(name);
        TextBox snippet;
        snippet.Header(box_value(Localize(L"InsertionTemplate")));
        snippet.PlaceholderText(LR"(\\frac{${1}}{${2}}$0)");
        snippet.AcceptsReturn(false);
        snippet.TextWrapping(TextWrapping::NoWrap);
        if (existing)
        {
            auto literal = folia::encode_snippet_literal(existing->snippet);
            snippet.Text(winrt::to_hstring(folia::cps_to_utf8(literal)));
        }
        fields.Children().Append(snippet);

        Grid assignment;
        assignment.ColumnSpacing(10);
        assignment.ColumnDefinitions().Append(ColumnDefinition{});
        assignment.ColumnDefinitions().Append(ColumnDefinition{});
        ComboBox scope;
        scope.Header(box_value(Localize(L"ShortcutScope")));
        scope.Items().Append(box_value(Localize(L"ShortcutScopeGlobal")));
        scope.Items().Append(box_value(Localize(L"ShortcutScopeCode")));
        scope.Items().Append(box_value(Localize(L"ShortcutScopeMath")));
        scope.SelectedIndex(existing ? ScopeIndex(existing->scope) : 0);
        assignment.Children().Append(scope);
        StackPanel gestureField;
        gestureField.Spacing(4);
        gestureField.Children().Append(Text(Localize(L"ShortcutKeys"), 12));
        Button gestureButton;
        std::optional<EditorKeyGesture> gesture = existing
            ? existing->gesture : std::nullopt;
        gestureButton.Content(box_value(GestureLabel(gesture)));
        gestureButton.HorizontalAlignment(HorizontalAlignment::Stretch);
        gestureButton.HorizontalContentAlignment(HorizontalAlignment::Center);
        bool capturing = false;
        gestureButton.Click([&gestureButton, &capturing](auto const&, auto const&)
        {
            capturing = true;
            gestureButton.Content(box_value(Localize(L"PressShortcut")));
            gestureButton.Focus(FocusState::Programmatic);
        });
        gestureButton.KeyDown([&gestureButton, &gesture, &capturing](
            auto const&, Input::KeyRoutedEventArgs const& args)
        {
            if (!capturing) return;
            auto key = args.Key();
            if (key == Windows::System::VirtualKey::Escape)
            {
                capturing = false;
                gestureButton.Content(box_value(GestureLabel(gesture)));
                args.Handled(true);
                return;
            }
            if (IsModifierKey(key))
            {
                args.Handled(true);
                return;
            }
            auto candidate = EditorKeyGesture{
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
                && !candidate.control && !candidate.shift && !candidate.alt)
                gesture.reset();
            else
                gesture = candidate;
            capturing = false;
            gestureButton.Content(box_value(GestureLabel(gesture)));
            args.Handled(true);
        });
        gestureField.Children().Append(gestureButton);
        Grid::SetColumn(gestureField, 1);
        assignment.Children().Append(gestureField);
        fields.Children().Append(assignment);

        TextBlock error;
        error.TextWrapping(TextWrapping::Wrap);
        error.Foreground(Brush({196, 43, 28, 255}));
        fields.Children().Append(error);
        dialog.Content(fields);

        dialog.PrimaryButtonClick([this, &name, &snippet, &scope, &gesture, &error, index](
            auto const&, ContentDialogButtonClickEventArgs const& args)
        {
            auto literal = folia::utf8_to_cps(winrt::to_string(snippet.Text()));
            auto decoded = folia::decode_snippet_literal(literal);
            if (!decoded)
            {
                error.Text(LocalizeFormat(L"SnippetLiteralError", {
                    winrt::to_hstring(decoded.error_offset.value_or(0) + 1)}));
                args.Cancel(true);
                return;
            }

            auto candidate = shortcutModel_;
            folia::platform::editor::ShortcutSettingsResult result;
            if (index)
            {
                result = candidate.SetGesture(*index, std::nullopt);
                if (result)
                    result = candidate.UpdateSnippet(
                        *index,
                        winrt::to_string(name.Text()),
                        std::move(decoded.value),
                        ScopeFromIndex(scope.SelectedIndex()));
                if (result) result = candidate.SetGesture(*index, gesture);
            }
            else
            {
                auto id = std::string("snippet.") + std::to_string(GetTickCount64());
                while (std::ranges::find(candidate.Bindings(), id, &ShortcutBinding::id)
                    != candidate.Bindings().end()) id.push_back('x');
                result = candidate.AddSnippet({
                    .id = std::move(id),
                    .custom_name = winrt::to_string(name.Text()),
                    .scope = ScopeFromIndex(scope.SelectedIndex()),
                    .gesture = gesture,
                    .snippet = std::move(decoded.value),
                });
            }
            if (!HandleShortcutResult(result, gesture))
            {
                error.Text(shortcutStatus_.Text());
                args.Cancel(true);
                return;
            }
            shortcutModel_ = std::move(candidate);
            if (!ApplyShortcutSettings())
            {
                error.Text(shortcutStatus_.Text());
                args.Cancel(true);
                return;
            }
            RefreshShortcutList();
        });

        co_await dialog.ShowAsync();
    }

    void SettingsView::RemoveSnippet(std::size_t index)
    {
        auto result = shortcutModel_.RemoveSnippet(index);
        if (!HandleShortcutResult(result)) return;
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }
}

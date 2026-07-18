#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

import folia.core.utf;

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
    using settings_ui::Card;
    using settings_ui::PageHeading;
    using settings_ui::Text;

    UIElement SettingsView::BuildShortcutsPage()
    {
        Grid page;
        page.Margin(Thickness{24, 18, 24, 24});
        page.RowSpacing(12);
        for (auto index = 0; index < 6; ++index)
        {
            RowDefinition row;
            row.Height(index == 3
                ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                : GridLengthHelper::Auto());
            page.RowDefinitions().Append(row);
        }

        auto heading = PageHeading(Localize(L"Shortcuts"));
        Grid::SetRow(heading, 0);
        page.Children().Append(heading);
        auto description = Text(Localize(L"ShortcutsDescription"));
        Grid::SetRow(description, 1);
        page.Children().Append(description);

        Grid tableHeader;
        tableHeader.ColumnSpacing(12);
        for (auto width : {2.0, 1.0, 1.2, 0.8})
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
            tableHeader.ColumnDefinitions().Append(column);
        }
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
        Grid::SetRow(tableHeader, 2);
        page.Children().Append(tableHeader);

        shortcutList_.SelectionMode(ListViewSelectionMode::None);
        shortcutList_.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        Grid::SetRow(shortcutList_, 3);
        page.Children().Append(shortcutList_);

        StackPanel snippetCard;
        snippetCard.Spacing(8);
        auto snippetHeading = Text(Localize(L"CustomInsertionAction"), 18);
        snippetHeading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        snippetCard.Children().Append(snippetHeading);
        snippetCard.Children().Append(Text(Localize(L"SnippetPlaceholderDescription")));
        Grid form;
        form.ColumnSpacing(8);
        for (auto width : {1.0, 2.0, 0.8})
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
            form.ColumnDefinitions().Append(column);
        }
        snippetNameBox_.Header(box_value(Localize(L"ActionName")));
        snippetNameBox_.PlaceholderText(Localize(L"ActionNameExample"));
        form.Children().Append(snippetNameBox_);
        snippetTemplateBox_.Header(box_value(Localize(L"InsertionTemplate")));
        snippetTemplateBox_.PlaceholderText(LR"(\frac{$1}{$2}$0)");
        snippetTemplateBox_.AcceptsReturn(true);
        snippetTemplateBox_.TextWrapping(TextWrapping::Wrap);
        snippetTemplateBox_.MinHeight(88);
        snippetTemplateBox_.MaxHeight(160);
        ScrollViewer::SetVerticalScrollBarVisibility(
            snippetTemplateBox_, ScrollBarVisibility::Auto);
        Grid::SetColumn(snippetTemplateBox_, 1);
        form.Children().Append(snippetTemplateBox_);
        snippetScopeBox_.Header(box_value(Localize(L"ShortcutScope")));
        snippetScopeBox_.Items().Append(box_value(Localize(L"ShortcutScopeGlobal")));
        snippetScopeBox_.Items().Append(box_value(Localize(L"ShortcutScopeCode")));
        snippetScopeBox_.Items().Append(box_value(Localize(L"ShortcutScopeMath")));
        snippetScopeBox_.SelectedIndex(0);
        Grid::SetColumn(snippetScopeBox_, 2);
        form.Children().Append(snippetScopeBox_);
        snippetCard.Children().Append(form);

        StackPanel snippetButtons;
        snippetButtons.Orientation(Orientation::Horizontal);
        snippetButtons.Spacing(8);
        saveSnippetButton_.Content(box_value(Localize(L"AddInsertionAction")));
        saveSnippetButton_.Click([this](auto const&, auto const&) { SaveSnippet(); });
        cancelSnippetButton_.Content(box_value(Localize(L"Cancel")));
        cancelSnippetButton_.Visibility(Visibility::Collapsed);
        cancelSnippetButton_.Click([this](auto const&, auto const&) { ResetSnippetForm(); });
        snippetButtons.Children().Append(saveSnippetButton_);
        snippetButtons.Children().Append(cancelSnippetButton_);
        snippetCard.Children().Append(snippetButtons);
        auto card = Card(snippetCard);
        Grid::SetRow(card, 4);
        page.Children().Append(card);

        shortcutStatus_.TextWrapping(TextWrapping::Wrap);
        Grid::SetRow(shortcutStatus_, 5);
        page.Children().Append(shortcutStatus_);
        RefreshShortcutList();
        return page;
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
        shortcutList_.Items().Clear();
        auto const& bindings = shortcutModel_.Bindings();
        shortcutButtons_.reserve(bindings.size());

        for (std::size_t index = 0; index < bindings.size(); ++index)
        {
            auto const& binding = bindings[index];
            Grid row;
            row.Padding(Thickness{4, 4, 4, 4});
            row.ColumnSpacing(12);
            for (auto width : {2.0, 1.0, 1.2, 0.8})
            {
                ColumnDefinition column;
                column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
                row.ColumnDefinitions().Append(column);
            }

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

            StackPanel operations;
            operations.Orientation(Orientation::Horizontal);
            operations.Spacing(6);
            Button clear;
            clear.Content(box_value(Localize(L"Clear")));
            clear.Click([this, index](auto const&, auto const&) { ClearShortcut(index); });
            operations.Children().Append(clear);
            if (binding.action_kind
                == folia::platform::editor::EditorShortcutActionKind::InsertSnippet)
            {
                Button edit;
                edit.Content(box_value(Localize(L"Edit")));
                edit.Click([this, index](auto const&, auto const&) { EditSnippet(index); });
                operations.Children().Append(edit);
                Button remove;
                remove.Content(box_value(Localize(L"Remove")));
                remove.Click([this, index](auto const&, auto const&) { RemoveSnippet(index); });
                operations.Children().Append(remove);
            }
            Grid::SetColumn(operations, 3);
            row.Children().Append(operations);

            ListViewItem item;
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            item.Content(row);
            shortcutList_.Items().Append(item);
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

    void SettingsView::EditSnippet(std::size_t index)
    {
        auto const& bindings = shortcutModel_.Bindings();
        if (index >= bindings.size()) return;
        auto const& binding = bindings[index];
        if (binding.action_kind
            != folia::platform::editor::EditorShortcutActionKind::InsertSnippet) return;
        editingSnippet_ = index;
        snippetNameBox_.Text(winrt::to_hstring(binding.custom_name));
        snippetTemplateBox_.Text(winrt::to_hstring(folia::cps_to_utf8(binding.snippet)));
        snippetScopeBox_.SelectedIndex(ScopeIndex(binding.scope));
        saveSnippetButton_.Content(box_value(Localize(L"SaveInsertionAction")));
        cancelSnippetButton_.Visibility(Visibility::Visible);
    }

    void SettingsView::SaveSnippet()
    {
        auto name = winrt::to_string(snippetNameBox_.Text());
        auto source = winrt::to_string(snippetTemplateBox_.Text());
        auto scope = ScopeFromIndex(snippetScopeBox_.SelectedIndex());
        folia::platform::editor::ShortcutSettingsResult result;
        std::optional<EditorKeyGesture> gesture;
        if (editingSnippet_ && *editingSnippet_ < shortcutModel_.Bindings().size())
        {
            gesture = shortcutModel_.Bindings()[*editingSnippet_].gesture;
            result = shortcutModel_.UpdateSnippet(
                *editingSnippet_, std::move(name), folia::utf8_to_cps(source), scope);
        }
        else
        {
            auto id = std::string("snippet.") + std::to_string(GetTickCount64());
            while (std::ranges::find(shortcutModel_.Bindings(), id, &ShortcutBinding::id)
                != shortcutModel_.Bindings().end()) id.push_back('x');
            result = shortcutModel_.AddSnippet({
                .id = id,
                .custom_name = std::move(name),
                .scope = scope,
                .snippet = folia::utf8_to_cps(source),
            });
        }
        if (!HandleShortcutResult(result, gesture)) return;
        if (ApplyShortcutSettings())
        {
            ResetSnippetForm();
            RefreshShortcutList();
        }
    }

    void SettingsView::RemoveSnippet(std::size_t index)
    {
        auto result = shortcutModel_.RemoveSnippet(index);
        if (!HandleShortcutResult(result)) return;
        if (editingSnippet_)
        {
            if (*editingSnippet_ == index) ResetSnippetForm();
            else if (*editingSnippet_ > index) --*editingSnippet_;
        }
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }

    void SettingsView::ResetSnippetForm()
    {
        editingSnippet_.reset();
        snippetNameBox_.Text({});
        snippetTemplateBox_.Text({});
        snippetScopeBox_.SelectedIndex(0);
        saveSnippetButton_.Content(box_value(Localize(L"AddInsertionAction")));
        cancelSnippetButton_.Visibility(Visibility::Collapsed);
    }
}

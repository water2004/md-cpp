#include "pch.h"
#include "editor/interaction/EditorKeyboardController.h"

import folia.core.snippet_template;

namespace winrt::Folia
{
    bool EditorKeyboardController::InsertSnippet(
        std::u32string_view source,
        std::optional<std::u32string> selectedText)
    {
        if (!session_ || !executeCommand_) return false;
        auto before = session_->Selection();
        if (!selectedText && !before.is_caret())
        {
            if (before.anchor.container_id == before.active.container_id)
            {
                if (auto editable = session_->EditableSource(before.active.container_id))
                {
                    auto begin = (std::min)(
                        before.anchor.source_offset, before.active.source_offset);
                    auto end = (std::max)(
                        before.anchor.source_offset, before.active.source_offset);
                    if (end <= editable->size())
                        selectedText = editable->substr(begin, end - begin);
                }
            }
            if (!selectedText) selectedText = session_->SelectedSource();
        }
        folia::SnippetExpansionContext context;
        if (selectedText) context.selected_text = *selectedText;
        auto parsed = folia::parse_snippet_template(source, context);
        CancelSnippetSession();
        if (parsed.text.empty())
        {
            if (!before.is_caret()
                && !executeCommand_(folia::Command{
                    .kind = folia::CommandKind::DeleteSelection}))
                return false;
        }
        else if (!executeCommand_(folia::Command::InsertText(parsed.text))) return false;
        if (parsed.tab_stops.empty()) return true;
        auto after = session_->Selection();
        if (session_->IsSourceMode()
            && std::ranges::contains(parsed.text, U'\n'))
            return true;
        if (after.active.source_offset < parsed.text.size()) return true;

        auto base = after.active.source_offset - parsed.text.size();
        auto selection = snippetSession_.Start(
            after.active.container_id, base, parsed.tab_stops);
        if (!selection) return true;
        session_->SetSelection(*selection);
        if (textInput_) textInput_->NotifySelectionChanged();
        if (render_) render_();
        return true;
    }

    bool EditorKeyboardController::InsertSnippetReplacing(
        folia::NodeId container,
        folia::SourceRange replacement,
        std::u32string_view source)
    {
        if (!session_ || replacement.start > replacement.end) return false;
        auto selection = session_->Selection();
        if (selection.active.container_id != container
            || selection.anchor.container_id != container)
            return false;
        auto editable = session_->EditableSource(container);
        if (!editable || !replacement.valid_for(editable->size())) return false;
        auto start = folia::TextPosition{
            container, replacement.start, folia::TextAffinity::Downstream};
        auto end = folia::TextPosition{
            container, replacement.end, folia::TextAffinity::Upstream};
        session_->SetSelection(start, end);
        if (textInput_) textInput_->NotifySelectionChanged();
        return InsertSnippet(source, std::u32string{});
    }

    bool EditorKeyboardController::MoveSnippetTabStop(bool backward)
    {
        if (!session_) return false;
        auto result = snippetSession_.Navigate(session_->Selection(), backward);
        if (!result.Handled()) return false;
        if (!result.selection) return true;
        session_->SetSelection(*result.selection);
        if (textInput_) textInput_->NotifySelectionChanged();
        if (render_) render_();
        return true;
    }

    void EditorKeyboardController::CancelSnippetSession()
    {
        snippetSession_.Cancel();
    }

    std::vector<folia::platform::editor::EditorSnippetPlaceholder>
    EditorKeyboardController::SnippetPlaceholders()
    {
        if (!session_) return {};
        return snippetSession_.Placeholders(session_->Selection());
    }
}

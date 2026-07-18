#include "pch.h"
#include "editor/session/EditorSession.h"

import folia.core.editor;
import folia.core.document_text;
import folia.core.document_interaction;
import folia.core.document_content_context;
import folia.core.document_footnotes;
import folia.core.render_builder;
import folia.core.render_model;
import folia.core.search;
import folia.core.serializer;
import folia.core.source_editor;
import folia.core.source_render;
import folia.core.utf;

namespace winrt::Folia
{
    namespace
    {
        std::wstring BoundaryWide(std::u32string_view text)
        {
            std::wstring result;
            for (auto codepoint : text)
            {
                if (codepoint <= 0xffff) result.push_back(static_cast<wchar_t>(codepoint));
                else
                {
                    codepoint -= 0x10000;
                    result.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
                    result.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
                }
            }
            return result;
        }

        std::size_t Utf16OffsetForCodepoint(
            std::u32string_view text,
            std::size_t codepointOffset)
        {
            codepointOffset = (std::min)(codepointOffset, text.size());
            std::size_t result = 0;
            for (std::size_t index = 0; index < codepointOffset; ++index)
                result += text[index] > 0xffff ? 2u : 1u;
            return result;
        }

        std::size_t CodepointOffsetForUtf16(
            std::u32string_view text,
            std::size_t utf16Offset)
        {
            std::size_t consumed = 0;
            std::size_t result = 0;
            while (result < text.size())
            {
                auto const units = text[result] > 0xffff ? 2u : 1u;
                if (consumed + units > utf16Offset) break;
                consumed += units;
                ++result;
            }
            return result;
        }

        // Mode switches consume the serializer's exact source-boundary map.
        // This remains a boundary projection and never becomes editor state.
        struct SerializedSourceProjection
        {
            explicit SerializedSourceProjection(folia::SerializedMarkdownProjection value)
                : projection(std::move(value)) {}

            std::size_t SourceOffset(folia::TextPosition position) const
            {
                return folia::serialized_offset_for_source_position(projection, position)
                    .value_or((std::min)(position.source_offset, projection.text.size()));
            }

            folia::TextPosition Position(std::size_t sourceOffset, folia::TextAffinity affinity) const
            {
                return folia::source_position_for_serialized_offset(projection, sourceOffset, affinity)
                    .value_or(folia::TextPosition{});
            }

            folia::SerializedMarkdownProjection projection;
        };

        bool ExecuteSourceCommand(folia::SourceEditor& editor, folia::Command const& command)
        {
            using folia::CommandKind;
            switch (command.kind)
            {
                case CommandKind::Undo: return editor.undo();
                case CommandKind::Redo: return editor.redo();
                case CommandKind::InsertText:
                case CommandKind::Paste: return editor.insert_text(command.text);
                case CommandKind::InsertNewline:
                case CommandKind::InsertSoftBreak: return editor.insert_newline();
                case CommandKind::DeleteBackward: return editor.delete_backward();
                case CommandKind::DeleteForward: return editor.delete_forward();
                case CommandKind::DeleteSelection:
                    return editor.selection().is_caret() ? false : editor.replace_selection({});
                case CommandKind::SelectAll: editor.select_all(); return true;
                case CommandKind::MoveLeft: editor.move_left(command.extend_selection); return true;
                case CommandKind::MoveRight: editor.move_right(command.extend_selection); return true;
                case CommandKind::MoveLineStart: editor.move_line_start(command.extend_selection); return true;
                case CommandKind::MoveLineEnd: editor.move_line_end(command.extend_selection); return true;
                case CommandKind::MoveDocumentStart: editor.move_document_start(command.extend_selection); return true;
                case CommandKind::MoveDocumentEnd: editor.move_document_end(command.extend_selection); return true;
                case CommandKind::IndentListItem: return editor.indent();
                case CommandKind::OutdentListItem: return editor.outdent();
                default: return false;
            }
        }
    }
    EditorSession::EditorSession() : core_(std::make_unique<detail::EditorSessionCore>())
    {
        RebuildCore();
    }

    EditorSession::~EditorSession() = default;
    EditorSession::EditorSession(EditorSession&&) noexcept = default;
    EditorSession& EditorSession::operator=(EditorSession&&) noexcept = default;

    void EditorSession::Open(
        winrt::Windows::Storage::StorageFile const& file,
        winrt::hstring const& text,
        LoadProgress progress)
    {
        file_ = file;
        text_ = text;
        ++revision_;
        RebuildCore(std::move(progress));
    }

    void EditorSession::SaveAs(winrt::Windows::Storage::StorageFile const& file)
    {
        file_ = file;
        core_->baseDirectory = std::filesystem::path(file_.Path().c_str()).parent_path().wstring();
    }

    void EditorSession::SetText(winrt::hstring const& text)
    {
        text_ = text;
        ++revision_;
        RebuildCore();
    }

    void EditorSession::SetTheme(folia::ThemeProfile const& theme)
    {
        core_->theme = theme;
        RebuildRenderModel();
    }

    void EditorSession::RebuildCore(LoadProgress progress)
    {
        ClearSearch();
        core_->renderedSearchFragments.clear();
        core_->renderedSearchRevision = (std::numeric_limits<std::uint64_t>::max)();
        core_->baseDirectory = file_ ? std::filesystem::path(file_.Path().c_str()).parent_path().wstring() : std::wstring{};
        core_->sourceEditor.reset();
        core_->editor = folia::Editor(
            winrt::to_string(text_),
            folia::default_dialect(),
            std::move(progress));
        text_ = {};
        RefreshCharacterCount();
        RebuildRenderModel();
    }

    void EditorSession::RebuildRenderModel(folia::EditorDocumentChange const* change)
    {
        if (core_->sourceEditor)
        {
            core_->renderModel = folia::build_source_render_model_incremental(
                *core_->sourceEditor,
                std::move(core_->renderModel));
        }
        else if (change)
        {
            folia::RenderModelUpdate update;
            update.structural = change->structural;
            update.structural_locality_known = change->structural_locality_known;
            update.structural_anchors = change->structural_anchors;
            std::unordered_set<std::uint64_t> owners;
            for (auto const& operation : change->text_operations)
            {
                auto const& edit = change->forward ? operation.forward : operation.inverse;
                if (owners.insert(edit.container_id.v).second)
                {
                    update.changed_owners.push_back(edit.container_id);
                }
            }
            core_->renderModel = folia::build_render_model_incremental(
                core_->editor.document(),
                core_->editor.outline(),
                core_->editor.symbols(),
                core_->theme,
                std::move(core_->renderModel),
                update);
        }
        else
        {
            core_->renderModel = ShouldVirtualizeRenderModel()
                ? folia::build_virtualized_render_model(
                    core_->editor.document(),
                    core_->editor.outline(),
                    core_->editor.symbols(),
                    core_->theme)
                : folia::build_render_model(
                    core_->editor.document(),
                    core_->editor.outline(),
                    core_->editor.symbols(),
                    core_->theme);
        }
        core_->renderModel.revision = revision_;
    }

    bool EditorSession::ShouldVirtualizeRenderModel() const
    {
        if (!core_ || core_->sourceEditor) return false;
        auto const& document = core_->editor.document();
        return document.root.children.size() > 4096
            || document.cached_editable_order.size() > 8192;
    }

    void EditorSession::MaterializeRenderBlocks(std::size_t begin, std::size_t end)
    {
        if (!core_ || core_->sourceEditor || !core_->renderModel.virtualized) return;
        folia::materialize_render_model_range(
            core_->renderModel,
            core_->editor.document(),
            core_->editor.symbols(),
            core_->theme,
            begin,
            end);
    }

    void EditorSession::ReleaseRenderBlocksOutside(std::size_t begin, std::size_t end)
    {
        if (!core_ || core_->sourceEditor || !core_->renderModel.virtualized) return;
        folia::release_render_model_blocks_outside(
            core_->renderModel,
            core_->editor.document(),
            core_->theme,
            begin,
            end);
    }

    bool EditorSession::IsSourceMode() const
    {
        return core_ && core_->sourceEditor.has_value();
    }

    bool EditorSession::EnterSourceMode()
    {
        if (IsSourceMode()) return false;
        ClearSearch();
        core_->renderedSearchFragments.clear();
        SerializedSourceProjection projection(folia::serialize_markdown_projection(core_->editor.document()));
        auto richSelection = core_->editor.selection();
        auto sourceSelection = folia::SourceSelection{
            projection.SourceOffset(richSelection.anchor),
            projection.SourceOffset(richSelection.active),
            richSelection.anchor.affinity,
            richSelection.active.affinity,
        };
        core_->sourceEditor.emplace(std::move(projection.projection.text));
        core_->sourceEditor->set_selection({
            sourceSelection.anchor,
            sourceSelection.active,
            sourceSelection.anchor_affinity,
            sourceSelection.active_affinity,
        });
        ++revision_;
        core_->characterCount = core_->sourceEditor->source().size();
        RebuildRenderModel();
        return true;
    }

    bool EditorSession::ExitSourceMode()
    {
        if (!IsSourceMode()) return false;
        ClearSearch();
        core_->renderedSearchFragments.clear();
        auto sourceSelection = core_->sourceEditor->selection();
        if (core_->sourceEditor->dirty())
        {
            core_->editor = folia::Editor(folia::cps_to_utf8(core_->sourceEditor->source()));
        }
        SerializedSourceProjection projection(folia::serialize_markdown_projection(core_->editor.document()));
        auto anchor = projection.Position(sourceSelection.anchor, sourceSelection.anchor_affinity);
        auto active = projection.Position(sourceSelection.active, sourceSelection.active_affinity);
        core_->sourceEditor.reset();
        if (core_->editor.editable_source(anchor.container_id)
            && core_->editor.editable_source(active.container_id))
        {
            core_->editor.set_selection({anchor, active});
        }
        ++revision_;
        RefreshCharacterCount();
        RebuildRenderModel();
        return true;
    }

    bool EditorSession::ToggleSourceMode()
    {
        return IsSourceMode() ? ExitSourceMode() : EnterSourceMode();
    }

    bool EditorSession::ExecuteCommand(folia::Command const& command)
    {
        if (core_->sourceEditor)
        {
            auto previousRevision = core_->sourceEditor->revision();
            if (!ExecuteSourceCommand(*core_->sourceEditor, command)) return false;
            if (core_->sourceEditor->revision() == previousRevision) return true;
            ++revision_;
            core_->characterCount = core_->sourceEditor->source().size();
            RebuildRenderModel();
            return true;
        }
        auto previousDocumentRevision = core_->editor.revision();
        if (command.kind == folia::CommandKind::Undo)
        {
            auto undone = core_->editor.undo();
            if (!undone)
            {
                return false;
            }
        }
        else if (command.kind == folia::CommandKind::Redo)
        {
            auto redone = core_->editor.redo();
            if (!redone)
            {
                return false;
            }
        }
        else if (command.kind == folia::CommandKind::SelectAll)
        {
            return core_->editor.execute_command(command);
        }
        else
        {
            auto executed = core_->editor.execute_command(command);
            if (!executed)
            {
                return false;
            }
        }

        if (core_->editor.revision() != previousDocumentRevision)
        {
            ++revision_;
            auto change = core_->editor.take_last_document_change();
            if (!change || change->structural)
            {
                RefreshCharacterCount();
            }
            else
            {
                std::ptrdiff_t delta = 0;
                for (auto const& operation : change->text_operations)
                {
                    auto const& edit = change->forward ? operation.forward : operation.inverse;
                    delta += static_cast<std::ptrdiff_t>(edit.replacement.size());
                    delta -= static_cast<std::ptrdiff_t>(edit.range.length());
                }
                if (delta < 0 && core_->characterCount < static_cast<std::size_t>(-delta))
                    RefreshCharacterCount();
                else
                    core_->characterCount = static_cast<std::size_t>(
                        static_cast<std::ptrdiff_t>(core_->characterCount) + delta);
            }
            RebuildRenderModel(change ? &*change : nullptr);
        }
        return true;
    }

    void EditorSession::SetSelection(folia::TextPosition anchor, folia::TextPosition active)
    {
        if (core_->sourceEditor)
        {
            auto anchorOffset = core_->sourceEditor->source_offset_from_position(anchor);
            auto activeOffset = core_->sourceEditor->source_offset_from_position(active);
            if (!anchorOffset || !activeOffset) return;
            core_->sourceEditor->set_selection({*anchorOffset, *activeOffset, anchor.affinity, active.affinity});
            return;
        }
        auto anchorSource = core_->editor.editable_source(anchor.container_id);
        auto activeSource = core_->editor.editable_source(active.container_id);
        if (!anchorSource || !activeSource) return;
        anchor.source_offset = (std::min)(anchor.source_offset, anchorSource->size());
        active.source_offset = (std::min)(active.source_offset, activeSource->size());
        core_->editor.set_selection({anchor, active});
    }

    void EditorSession::SetSelection(folia::TextSelection selection)
    {
        SetSelection(selection.anchor, selection.active);
    }

    bool EditorSession::HasSelection() const
    {
        return core_->sourceEditor
            ? !core_->sourceEditor->selection().is_caret()
            : !core_->editor.selection().is_caret();
    }

    std::u32string EditorSession::SelectedSource() const
    {
        return core_->sourceEditor
            ? core_->sourceEditor->selected_text()
            : core_->editor.selected_markdown_cps();
    }

    std::string EditorSession::SelectedTextUtf8() const
    {
        return folia::cps_to_utf8(SelectedSource());
    }

    bool EditorSession::HasFile() const
    {
        return file_ != nullptr;
    }

    winrt::Windows::Storage::StorageFile EditorSession::File() const
    {
        return file_;
    }

    winrt::hstring EditorSession::Text() const
    {
        if (!core_) return text_;
        return winrt::to_hstring(core_->sourceEditor
            ? folia::cps_to_utf8(core_->sourceEditor->source())
            : core_->editor.markdown_utf8());
    }

    winrt::hstring EditorSession::DisplayName() const
    {
        return file_ ? file_.Name() : L"Untitled.md";
    }

    winrt::hstring EditorSession::Path() const
    {
        return file_ ? file_.Path() : L"";
    }

    uint64_t EditorSession::Revision() const
    {
        return revision_;
    }

    void EditorSession::RefreshCharacterCount()
    {
        core_->characterCount = core_->sourceEditor
            ? core_->sourceEditor->source().size()
            : folia::document_text_character_count(core_->editor.document());
    }

    std::size_t EditorSession::CharacterCount() const
    {
        return core_->characterCount;
    }

    std::wstring EditorSession::TextInputTextUtf16(folia::NodeId containerId) const
    {
        auto source = TextInputSourceView(containerId);
        return source ? BoundaryWide(*source) : std::wstring{};
    }

    std::size_t EditorSession::TextInputAcpOffset(folia::TextPosition position) const
    {
        auto source = TextInputSourceView(position.container_id);
        if (!source) return 0;
        auto const offset = (std::min)(position.source_offset, source->size());
        return Utf16OffsetForCodepoint(*source, offset);
    }

    folia::TextPosition EditorSession::TextInputPositionFromAcp(
        folia::NodeId containerId,
        std::size_t offset,
        folia::TextAffinity affinity) const
    {
        auto source = TextInputSourceView(containerId);
        if (!source) return {};
        auto localOffset = CodepointOffsetForUtf16(*source, offset);
        return {containerId, (std::min)(localOffset, source->size()), affinity};
    }

    std::optional<std::u32string_view> EditorSession::TextInputSourceView(
        folia::NodeId containerId) const
    {
        if (core_->sourceEditor)
        {
            auto found = std::ranges::find(
                core_->sourceEditor->lines(), containerId, &folia::SourceLine::id);
            if (found == core_->sourceEditor->lines().end()) return std::nullopt;
            return std::u32string_view{found->text};
        }
        return folia::document_editable_text_view(core_->editor.document(), containerId);
    }

    std::optional<std::u32string> EditorSession::EditableSource(folia::NodeId id) const
    {
        if (core_->sourceEditor)
        {
            auto found = std::ranges::find(core_->sourceEditor->lines(), id, &folia::SourceLine::id);
            if (found == core_->sourceEditor->lines().end()) return std::nullopt;
            return found->text;
        }
        return core_->editor.editable_source(id);
    }

    std::optional<EditorDocumentInteraction> EditorSession::InteractionAt(folia::TextPosition position) const
    {
        if (core_->sourceEditor) return std::nullopt;
        auto interaction = folia::document_interaction_at(core_->editor.document(), position);
        if (!interaction) return std::nullopt;
        return EditorDocumentInteraction{
            std::move(interaction->link),
            std::move(interaction->tooltip),
        };
    }

    folia::TextSelection EditorSession::Selection() const
    {
        return core_->sourceEditor ? core_->sourceEditor->projected_selection() : core_->editor.selection();
    }

    folia::platform::editor::EditorShortcutScope EditorSession::ShortcutScope() const
    {
        using folia::DocumentContentContext;
        using folia::platform::editor::EditorShortcutScope;
        if (core_->sourceEditor) return EditorShortcutScope::Global;
        switch (folia::document_content_context_at(
            core_->editor.document(), core_->editor.selection().active))
        {
            case DocumentContentContext::Code: return EditorShortcutScope::Code;
            case DocumentContentContext::Math: return EditorShortcutScope::Math;
            case DocumentContentContext::Normal: return EditorShortcutScope::Global;
        }
        return EditorShortcutScope::Global;
    }

    std::optional<EditorLatexCompletionSource> EditorSession::LatexCompletionSourceAtCaret() const
    {
        if (core_->sourceEditor) return std::nullopt;
        auto selection = core_->editor.selection();
        if (selection.anchor != selection.active) return std::nullopt;
        if (folia::document_content_context_at(core_->editor.document(), selection.active)
            != folia::DocumentContentContext::Math)
            return std::nullopt;
        auto source = folia::document_editable_text_view(
            core_->editor.document(), selection.active.container_id);
        if (!source) return std::nullopt;
        return EditorLatexCompletionSource{
            .container = selection.active.container_id,
            .source = *source,
            .caret = selection.active.source_offset,
        };
    }

    folia::RenderModel const& EditorSession::RenderModel() const
    {
        return core_->renderModel;
    }

    folia::RenderModel EditorSession::BuildPrintRenderModel() const
    {
        if (core_->sourceEditor) return core_->renderModel;
        return folia::build_render_model(
            core_->editor.document(),
            core_->editor.outline(),
            core_->editor.symbols(),
            core_->theme);
    }

    std::optional<folia::TextPosition> EditorSession::FootnoteDefinitionTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return folia::footnote_definition_target(
            core_->editor.document(), core_->editor.symbols(), label);
    }

    std::optional<folia::TextPosition> EditorSession::FirstFootnoteReferenceTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return folia::first_footnote_reference_target(
            core_->editor.symbols(), label);
    }

    std::string EditorSession::FootnotePreview(std::string_view label) const
    {
        if (core_->sourceEditor) return {};
        return folia::footnote_preview(
            core_->editor.document(), core_->editor.symbols(), label, 240);
    }

    std::wstring const& EditorSession::BaseDirectory() const
    {
        return core_->baseDirectory;
    }

    detail::EditorRenderFrame EditorSession::RenderFrame()
    {
        return detail::EditorRenderFrame{
            core_->renderModel,
            Selection(),
            core_->searchHighlights,
            core_->baseDirectory,
            core_->renderModel.virtualized
                ? std::function<void(std::size_t, std::size_t)>{
                    [this](auto begin, auto end) { MaterializeRenderBlocks(begin, end); }}
                : std::function<void(std::size_t, std::size_t)>{},
            core_->renderModel.virtualized
                ? std::function<void(std::size_t, std::size_t)>{
                    [this](auto begin, auto end) { ReleaseRenderBlocksOutside(begin, end); }}
                : std::function<void(std::size_t, std::size_t)>{},
        };
    }
}

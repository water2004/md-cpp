#include "pch.h"
#include "editor/session/EditorSession.h"

import elmd.core.editor;
import elmd.core.document_text;
import elmd.core.document_interaction;
import elmd.core.document_footnotes;
import elmd.core.render_builder;
import elmd.core.render_model;
import elmd.core.serializer;
import elmd.core.source_editor;
import elmd.core.source_render;
import elmd.core.utf;

namespace winrt::ElMd
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
            explicit SerializedSourceProjection(elmd::SerializedMarkdownProjection value)
                : projection(std::move(value)) {}

            std::size_t SourceOffset(elmd::TextPosition position) const
            {
                return elmd::serialized_offset_for_source_position(projection, position)
                    .value_or((std::min)(position.source_offset, projection.text.size()));
            }

            elmd::TextPosition Position(std::size_t sourceOffset, elmd::TextAffinity affinity) const
            {
                return elmd::source_position_for_serialized_offset(projection, sourceOffset, affinity)
                    .value_or(elmd::TextPosition{});
            }

            elmd::SerializedMarkdownProjection projection;
        };

        bool ExecuteSourceCommand(elmd::SourceEditor& editor, elmd::Command const& command)
        {
            using elmd::CommandKind;
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

    void EditorSession::Open(winrt::Windows::Storage::StorageFile const& file, winrt::hstring const& text)
    {
        file_ = file;
        text_ = text;
        ++revision_;
        RebuildCore();
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

    void EditorSession::SetTheme(elmd::ThemeProfile const& theme)
    {
        core_->theme = theme;
        RebuildRenderModel();
    }

    void EditorSession::RebuildCore()
    {
        core_->baseDirectory = file_ ? std::filesystem::path(file_.Path().c_str()).parent_path().wstring() : std::wstring{};
        core_->sourceEditor.reset();
        core_->editor = elmd::Editor(winrt::to_string(text_));
        text_ = {};
        RefreshCharacterCount();
        RebuildRenderModel();
    }

    void EditorSession::RebuildRenderModel(elmd::EditorDocumentChange const* change)
    {
        if (core_->sourceEditor)
        {
            core_->renderModel = elmd::build_source_render_model_incremental(
                *core_->sourceEditor,
                std::move(core_->renderModel));
        }
        else if (change)
        {
            elmd::RenderModelUpdate update;
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
            core_->renderModel = elmd::build_render_model_incremental(
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
                ? elmd::build_virtualized_render_model(
                    core_->editor.document(),
                    core_->editor.outline(),
                    core_->editor.symbols(),
                    core_->theme)
                : elmd::build_render_model(
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
        elmd::materialize_render_model_range(
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
        elmd::release_render_model_blocks_outside(
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
        SerializedSourceProjection projection(elmd::serialize_markdown_projection(core_->editor.document()));
        auto richSelection = core_->editor.selection();
        auto sourceSelection = elmd::SourceSelection{
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
        auto sourceSelection = core_->sourceEditor->selection();
        if (core_->sourceEditor->dirty())
        {
            core_->editor = elmd::Editor(elmd::cps_to_utf8(core_->sourceEditor->source()));
        }
        SerializedSourceProjection projection(elmd::serialize_markdown_projection(core_->editor.document()));
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

    bool EditorSession::ExecuteCommand(elmd::Command const& command)
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
        if (command.kind == elmd::CommandKind::Undo)
        {
            auto undone = core_->editor.undo();
            if (!undone)
            {
                return false;
            }
        }
        else if (command.kind == elmd::CommandKind::Redo)
        {
            auto redone = core_->editor.redo();
            if (!redone)
            {
                return false;
            }
        }
        else if (command.kind == elmd::CommandKind::SelectAll)
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

    void EditorSession::SetSelection(elmd::TextPosition anchor, elmd::TextPosition active)
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

    void EditorSession::SetSelection(elmd::TextSelection selection)
    {
        SetSelection(selection.anchor, selection.active);
    }

    bool EditorSession::HasSelection() const
    {
        return core_->sourceEditor
            ? !core_->sourceEditor->selection().is_caret()
            : !core_->editor.selection().is_caret();
    }

    std::string EditorSession::SelectedTextUtf8() const
    {
        return elmd::cps_to_utf8(core_->sourceEditor
            ? core_->sourceEditor->selected_text()
            : core_->editor.selected_markdown_cps());
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
            ? elmd::cps_to_utf8(core_->sourceEditor->source())
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
            : elmd::document_text_character_count(core_->editor.document());
    }

    std::size_t EditorSession::CharacterCount() const
    {
        return core_->characterCount;
    }

    std::wstring EditorSession::TextInputTextUtf16(elmd::NodeId containerId) const
    {
        auto source = TextInputSourceView(containerId);
        return source ? BoundaryWide(*source) : std::wstring{};
    }

    std::size_t EditorSession::TextInputAcpOffset(elmd::TextPosition position) const
    {
        auto source = TextInputSourceView(position.container_id);
        if (!source) return 0;
        auto const offset = (std::min)(position.source_offset, source->size());
        return Utf16OffsetForCodepoint(*source, offset);
    }

    elmd::TextPosition EditorSession::TextInputPositionFromAcp(
        elmd::NodeId containerId,
        std::size_t offset,
        elmd::TextAffinity affinity) const
    {
        auto source = TextInputSourceView(containerId);
        if (!source) return {};
        auto localOffset = CodepointOffsetForUtf16(*source, offset);
        return {containerId, (std::min)(localOffset, source->size()), affinity};
    }

    std::optional<std::u32string_view> EditorSession::TextInputSourceView(
        elmd::NodeId containerId) const
    {
        if (core_->sourceEditor)
        {
            auto found = std::ranges::find(
                core_->sourceEditor->lines(), containerId, &elmd::SourceLine::id);
            if (found == core_->sourceEditor->lines().end()) return std::nullopt;
            return std::u32string_view{found->text};
        }
        return elmd::document_editable_text_view(core_->editor.document(), containerId);
    }

    std::optional<std::u32string> EditorSession::EditableSource(elmd::NodeId id) const
    {
        if (core_->sourceEditor)
        {
            auto found = std::ranges::find(core_->sourceEditor->lines(), id, &elmd::SourceLine::id);
            if (found == core_->sourceEditor->lines().end()) return std::nullopt;
            return found->text;
        }
        return core_->editor.editable_source(id);
    }

    std::optional<EditorDocumentInteraction> EditorSession::InteractionAt(elmd::TextPosition position) const
    {
        if (core_->sourceEditor) return std::nullopt;
        auto interaction = elmd::document_interaction_at(core_->editor.document(), position);
        if (!interaction) return std::nullopt;
        return EditorDocumentInteraction{
            std::move(interaction->link),
            std::move(interaction->tooltip),
        };
    }

    elmd::TextSelection EditorSession::Selection() const
    {
        return core_->sourceEditor ? core_->sourceEditor->projected_selection() : core_->editor.selection();
    }

    elmd::RenderModel const& EditorSession::RenderModel() const
    {
        return core_->renderModel;
    }

    elmd::RenderModel EditorSession::BuildPrintRenderModel() const
    {
        if (core_->sourceEditor) return core_->renderModel;
        return elmd::build_render_model(
            core_->editor.document(),
            core_->editor.outline(),
            core_->editor.symbols(),
            core_->theme);
    }

    std::optional<elmd::TextPosition> EditorSession::FootnoteDefinitionTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return elmd::footnote_definition_target(
            core_->editor.document(), core_->editor.symbols(), label);
    }

    std::optional<elmd::TextPosition> EditorSession::FirstFootnoteReferenceTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return elmd::first_footnote_reference_target(
            core_->editor.symbols(), label);
    }

    std::string EditorSession::FootnotePreview(std::string_view label) const
    {
        if (core_->sourceEditor) return {};
        return elmd::footnote_preview(
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

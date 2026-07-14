#include "pch.h"
#include "EditorSession.h"

import elmd.core.editor;
import elmd.core.document_text;
import elmd.core.document_footnotes;
import elmd.core.render_builder;
import elmd.core.render_model;
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

        // TSF requires one flat UTF-16 ACP space. Keep that projection at the
        // WinUI boundary; the editor itself remains in block-local TextPosition
        // coordinates and never stores an ACP/global source offset.
        struct TextStoreProjection
        {
            explicit TextStoreProjection(elmd::EditorDocument const& document)
                : fragments(elmd::document_text_fragments(document))
            {
                for (std::size_t index = 0; index < fragments.size(); ++index)
                {
                    if (index) text.push_back(U'\n');
                    text += fragments[index].text;
                }
            }

            std::optional<std::size_t> CodepointOffset(elmd::TextPosition position) const
            {
                std::size_t offset = 0;
                for (auto const& fragment : fragments)
                {
                    if (fragment.container_id == position.container_id)
                    {
                        return offset + (std::min)(position.source_offset, fragment.text.size());
                    }
                    offset += fragment.text.size() + 1;
                }
                return std::nullopt;
            }

            std::optional<elmd::TextPosition> Position(
                std::size_t offset,
                elmd::TextAffinity affinity) const
            {
                for (std::size_t index = 0; index < fragments.size(); ++index)
                {
                    auto const& fragment = fragments[index];
                    if (offset <= fragment.text.size())
                    {
                        return elmd::TextPosition{fragment.container_id, offset, affinity};
                    }
                    offset -= fragment.text.size();
                    if (index + 1 < fragments.size())
                    {
                        if (offset == 0)
                        {
                            return elmd::TextPosition{
                                fragment.container_id,
                                fragment.text.size(),
                                affinity};
                        }
                        --offset;
                    }
                }
                if (fragments.empty()) return std::nullopt;
                auto const& last = fragments.back();
                return elmd::TextPosition{last.container_id, last.text.size(), affinity};
            }

            std::vector<elmd::DocumentTextFragment> fragments;
            std::u32string text;
        };

        // Mode switches are the only place where a rich block-local position
        // needs to be associated with the serialized Markdown source. This is
        // deliberately a boundary projection, never editor state.
        struct SerializedSourceProjection
        {
            explicit SerializedSourceProjection(elmd::EditorDocument const& document, std::u32string serialized)
                : text(std::move(serialized)), fragments(elmd::document_text_fragments(document))
            {
                std::size_t cursor = 0;
                locations.reserve(fragments.size());
                for (auto const& fragment : fragments)
                {
                    auto found = fragment.text.empty() ? cursor : text.find(fragment.text, cursor);
                    if (found == std::u32string::npos)
                    {
                        locations.push_back(std::nullopt);
                        continue;
                    }
                    locations.push_back(found);
                    cursor = found + fragment.text.size();
                }
            }

            std::size_t SourceOffset(elmd::TextPosition position) const
            {
                for (std::size_t index = 0; index < fragments.size(); ++index)
                {
                    if (fragments[index].container_id != position.container_id || !locations[index]) continue;
                    return *locations[index] + (std::min)(position.source_offset, fragments[index].text.size());
                }
                return (std::min)(position.source_offset, text.size());
            }

            elmd::TextPosition Position(std::size_t sourceOffset, elmd::TextAffinity affinity) const
            {
                sourceOffset = (std::min)(sourceOffset, text.size());
                std::optional<elmd::TextPosition> preceding;
                for (std::size_t index = 0; index < fragments.size(); ++index)
                {
                    if (!locations[index]) continue;
                    auto start = *locations[index];
                    auto end = start + fragments[index].text.size();
                    if (sourceOffset >= start && sourceOffset <= end)
                    {
                        return {fragments[index].container_id, sourceOffset - start, affinity};
                    }
                    if (end < sourceOffset)
                    {
                        preceding = elmd::TextPosition{fragments[index].container_id, fragments[index].text.size(), affinity};
                    }
                    else if (sourceOffset < start)
                    {
                        return {fragments[index].container_id, 0, affinity};
                    }
                }
                return preceding.value_or(elmd::TextPosition{});
            }

            std::u32string text;
            std::vector<elmd::DocumentTextFragment> fragments;
            std::vector<std::optional<std::size_t>> locations;
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

    void EditorSession::RebuildCore()
    {
        core_->baseDirectory = file_ ? std::filesystem::path(file_.Path().c_str()).parent_path().wstring() : std::wstring{};
        core_->sourceEditor.reset();
        core_->editor = elmd::Editor(winrt::to_string(text_));
        RebuildRenderModel();
    }

    void EditorSession::RebuildRenderModel()
    {
        core_->renderModel = core_->sourceEditor
            ? elmd::build_source_render_model(*core_->sourceEditor)
            : elmd::build_render_model(core_->editor.document(), core_->editor.outline());
        core_->renderModel.revision = revision_;
    }

    bool EditorSession::IsSourceMode() const
    {
        return core_ && core_->sourceEditor.has_value();
    }

    bool EditorSession::EnterSourceMode()
    {
        if (IsSourceMode()) return false;
        auto markdown = core_->editor.markdown_cps();
        SerializedSourceProjection projection(core_->editor.document(), markdown);
        auto richSelection = core_->editor.selection();
        core_->sourceEditor.emplace(std::move(markdown));
        core_->sourceEditor->set_selection({
            projection.SourceOffset(richSelection.anchor),
            projection.SourceOffset(richSelection.active),
            richSelection.anchor.affinity,
            richSelection.active.affinity,
        });
        ++revision_;
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
        SerializedSourceProjection projection(core_->editor.document(), core_->editor.markdown_cps());
        auto anchor = projection.Position(sourceSelection.anchor, sourceSelection.anchor_affinity);
        auto active = projection.Position(sourceSelection.active, sourceSelection.active_affinity);
        core_->sourceEditor.reset();
        if (core_->editor.editable_source(anchor.container_id)
            && core_->editor.editable_source(active.container_id))
        {
            core_->editor.set_selection({anchor, active});
        }
        ++revision_;
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
            RebuildRenderModel();
            return true;
        }
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

        revision_ = core_->editor.revision();
        RebuildRenderModel();
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

    std::wstring EditorSession::BoundaryTextUtf16() const
    {
        if (core_->sourceEditor) return BoundaryWide(core_->sourceEditor->source());
        return BoundaryWide(TextStoreProjection(core_->editor.document()).text);
    }

    std::size_t EditorSession::AcpLength() const
    {
        return BoundaryTextUtf16().size();
    }

    std::u32string EditorSession::TextView() const
    {
        if (core_->sourceEditor) return core_->sourceEditor->source();
        return TextStoreProjection(core_->editor.document()).text;
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

    elmd::TextSelection EditorSession::Selection() const
    {
        return core_->sourceEditor ? core_->sourceEditor->projected_selection() : core_->editor.selection();
    }

    std::size_t EditorSession::AcpOffset(elmd::TextPosition position) const
    {
        if (core_->sourceEditor)
        {
            auto sourceOffset = core_->sourceEditor->source_offset_from_position(position).value_or(0);
            return elmd::char_index_to_utf16(core_->sourceEditor->source(), sourceOffset);
        }
        TextStoreProjection projection(core_->editor.document());
        auto codepointOffset = projection.CodepointOffset(position).value_or(0);
        codepointOffset = (std::min)(codepointOffset, projection.text.size());
        return elmd::char_index_to_utf16(projection.text, codepointOffset);
    }

    elmd::TextPosition EditorSession::PositionFromAcp(std::size_t offset, elmd::TextAffinity affinity) const
    {
        if (core_->sourceEditor)
        {
            auto sourceOffset = elmd::utf16_to_char_index(core_->sourceEditor->source(), offset);
            return core_->sourceEditor->position_from_source_offset(sourceOffset, affinity);
        }
        TextStoreProjection projection(core_->editor.document());
        auto codepointOffset = elmd::utf16_to_char_index(projection.text, offset);
        return projection.Position(codepointOffset, affinity).value_or(elmd::TextPosition{});
    }

    elmd::RenderModel const& EditorSession::RenderModel() const
    {
        return core_->renderModel;
    }

    std::optional<elmd::TextPosition> EditorSession::FootnoteDefinitionTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return elmd::footnote_definition_target(core_->editor.document(), label);
    }

    std::optional<elmd::TextPosition> EditorSession::FirstFootnoteReferenceTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return elmd::first_footnote_reference_target(core_->editor.document(), label);
    }

    std::string EditorSession::FootnotePreview(std::string_view label) const
    {
        if (core_->sourceEditor) return {};
        return elmd::footnote_preview(core_->editor.document(), label, 240);
    }

    std::wstring const& EditorSession::BaseDirectory() const
    {
        return core_->baseDirectory;
    }

    detail::EditorRenderFrame EditorSession::RenderFrame() const
    {
        return detail::EditorRenderFrame{
            core_->renderModel,
            Selection(),
            core_->baseDirectory,
        };
    }
}

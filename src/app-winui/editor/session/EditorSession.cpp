#include "pch.h"
#include "editor/session/EditorSession.h"

import elmd.core.editor;
import elmd.core.document_text;
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

        std::vector<std::size_t> Utf16Offsets(std::u32string_view text)
        {
            std::vector<std::size_t> offsets;
            offsets.reserve(text.size() + 1);
            offsets.push_back(0);
            for (auto codepoint : text)
            {
                offsets.push_back(offsets.back() + elmd::utf16_len_of_cp(codepoint));
            }
            return offsets;
        }

        bool AddDelta(std::size_t& value, std::ptrdiff_t delta)
        {
            if (delta < 0 && value < static_cast<std::size_t>(-delta)) return false;
            value = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(value) + delta);
            return true;
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
        InvalidateBoundaryProjection();
        RebuildRenderModel();
    }

    void EditorSession::RebuildRenderModel(elmd::EditorDocumentChange const* change)
    {
        if (core_->sourceEditor)
        {
            core_->renderModel = elmd::build_source_render_model(*core_->sourceEditor);
        }
        else if (change)
        {
            elmd::RenderModelUpdate update;
            update.structural = change->structural;
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
            core_->renderModel = elmd::build_render_model(
                core_->editor.document(),
                core_->editor.outline(),
                core_->editor.symbols(),
                core_->theme);
        }
        core_->renderModel.revision = revision_;
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
        InvalidateBoundaryProjection();
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
        InvalidateBoundaryProjection();
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
            InvalidateBoundaryProjection();
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
            lastBoundaryTextChange_.reset();
            auto change = core_->editor.take_last_document_change();
            if (core_->boundaryProjection
                && (!change || !ApplyBoundaryProjectionChange(*change)))
            {
                InvalidateBoundaryProjection();
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

    void EditorSession::InvalidateBoundaryProjection()
    {
        core_->boundaryProjection.reset();
        lastBoundaryTextChange_.reset();
    }

    bool EditorSession::ApplyBoundaryProjectionChange(elmd::EditorDocumentChange const& change)
    {
        if (!core_->boundaryProjection || core_->sourceEditor || change.structural) return false;
        auto& projection = *core_->boundaryProjection;
        std::optional<detail::BoundaryTextChange> textChange;
        auto apply = [&](elmd::DocumentTextOperation const& operation)
        {
            auto const& edit = change.forward ? operation.forward : operation.inverse;
            auto found = projection.fragmentIndex.find(edit.container_id.v);
            if (found == projection.fragmentIndex.end()) return false;
            auto const index = found->second;
            if (index >= projection.fragments.size()) return false;
            auto& fragment = projection.fragments[index];
            if (!edit.range.valid_for(fragment.codepointLength)
                || fragment.codepointStart + fragment.codepointLength > projection.text.size()
                || edit.range.end >= fragment.codepointToUtf16.size())
            {
                return false;
            }

            auto const globalCodepointStart = fragment.codepointStart + edit.range.start;
            auto const globalUtf16Start = fragment.utf16Start
                + fragment.codepointToUtf16[edit.range.start];
            auto const globalUtf16End = fragment.utf16Start
                + fragment.codepointToUtf16[edit.range.end];
            auto replacementUtf16 = BoundaryWide(edit.replacement);
            if (change.text_operations.size() == 1u)
            {
                textChange = detail::BoundaryTextChange{
                    revision_ - 1,
                    revision_,
                    globalUtf16Start,
                    globalUtf16End - globalUtf16Start,
                    replacementUtf16,
                };
            }
            projection.text.replace(
                globalCodepointStart,
                edit.range.length(),
                edit.replacement);
            projection.utf16.replace(
                globalUtf16Start,
                globalUtf16End - globalUtf16Start,
                replacementUtf16);

            auto const codepointDelta = static_cast<std::ptrdiff_t>(edit.replacement.size())
                - static_cast<std::ptrdiff_t>(edit.range.length());
            auto const utf16Delta = static_cast<std::ptrdiff_t>(replacementUtf16.size())
                - static_cast<std::ptrdiff_t>(globalUtf16End - globalUtf16Start);
            if (!AddDelta(fragment.codepointLength, codepointDelta)
                || !AddDelta(fragment.utf16Length, utf16Delta))
            {
                return false;
            }
            fragment.codepointToUtf16 = Utf16Offsets(std::u32string_view{
                projection.text}.substr(fragment.codepointStart, fragment.codepointLength));
            fragment.utf16Length = fragment.codepointToUtf16.back();
            for (auto following = index + 1; following < projection.fragments.size(); ++following)
            {
                if (!AddDelta(projection.fragments[following].codepointStart, codepointDelta)
                    || !AddDelta(projection.fragments[following].utf16Start, utf16Delta))
                {
                    return false;
                }
            }
            return true;
        };

        if (change.forward)
        {
            for (auto const& operation : change.text_operations)
            {
                if (!apply(operation)) return false;
            }
        }
        else
        {
            for (auto operation = change.text_operations.rbegin();
                 operation != change.text_operations.rend(); ++operation)
            {
                if (!apply(*operation)) return false;
            }
        }

        for (auto const& operation : change.text_operations)
        {
            auto const& edit = change.forward ? operation.forward : operation.inverse;
            auto found = projection.fragmentIndex.find(edit.container_id.v);
            if (found == projection.fragmentIndex.end()) return false;
            auto const& fragment = projection.fragments[found->second];
            auto source = core_->editor.editable_source(edit.container_id);
            if (!source || source->size() != fragment.codepointLength
                || std::u32string_view{projection.text}.substr(
                    fragment.codepointStart, fragment.codepointLength) != *source)
            {
                return false;
            }
        }
        lastBoundaryTextChange_ = std::move(textChange);
        return true;
    }

    detail::BoundaryProjection const& EditorSession::BoundaryProjection() const
    {
        if (core_->boundaryProjection) return *core_->boundaryProjection;

        detail::BoundaryProjection projection;
        if (core_->sourceEditor)
        {
            projection.text = core_->sourceEditor->source();
            projection.fragments.reserve(core_->sourceEditor->lines().size());
            for (auto const& line : core_->sourceEditor->lines())
            {
                projection.fragmentIndex.emplace(line.id.v, projection.fragments.size());
                projection.fragments.push_back({line.id, line.source_start, line.text.size()});
            }
        }
        else
        {
            auto fragments = elmd::document_text_fragments(core_->editor.document());
            projection.fragments.reserve(fragments.size());
            for (std::size_t index = 0; index < fragments.size(); ++index)
            {
                if (index) projection.text.push_back(U'\n');
                projection.fragmentIndex.emplace(fragments[index].container_id.v, projection.fragments.size());
                projection.fragments.push_back({
                    fragments[index].container_id,
                    projection.text.size(),
                    fragments[index].text.size(),
                });
                projection.text += fragments[index].text;
            }
        }

        projection.utf16 = BoundaryWide(projection.text);
        auto globalOffsets = Utf16Offsets(projection.text);
        for (auto& fragment : projection.fragments)
        {
            auto const end = fragment.codepointStart + fragment.codepointLength;
            if (end > projection.text.size()) continue;
            fragment.utf16Start = globalOffsets[fragment.codepointStart];
            fragment.utf16Length = globalOffsets[end] - fragment.utf16Start;
            fragment.codepointToUtf16.reserve(fragment.codepointLength + 1);
            for (auto offset = fragment.codepointStart; offset <= end; ++offset)
            {
                fragment.codepointToUtf16.push_back(globalOffsets[offset] - fragment.utf16Start);
            }
        }
        core_->boundaryProjection.emplace(std::move(projection));
        return *core_->boundaryProjection;
    }

    std::wstring const& EditorSession::BoundaryTextUtf16() const
    {
        return BoundaryProjection().utf16;
    }

    std::optional<detail::BoundaryTextChange> const& EditorSession::LastBoundaryTextChange() const
    {
        return lastBoundaryTextChange_;
    }

    std::size_t EditorSession::AcpLength() const
    {
        return BoundaryProjection().utf16.size();
    }

    std::u32string const& EditorSession::TextView() const
    {
        return BoundaryProjection().text;
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
        auto const& projection = BoundaryProjection();
        auto found = projection.fragmentIndex.find(position.container_id.v);
        if (found == projection.fragmentIndex.end()) return 0;
        auto const& fragment = projection.fragments[found->second];
        auto const codepointOffset = (std::min)(position.source_offset, fragment.codepointLength);
        if (codepointOffset >= fragment.codepointToUtf16.size()) return fragment.utf16Start;
        return fragment.utf16Start + fragment.codepointToUtf16[codepointOffset];
    }

    elmd::TextPosition EditorSession::PositionFromAcp(std::size_t offset, elmd::TextAffinity affinity) const
    {
        auto const& projection = BoundaryProjection();
        if (projection.fragments.empty()) return {};
        offset = (std::min)(offset, projection.utf16.size());
        auto found = std::upper_bound(
            projection.fragments.begin(),
            projection.fragments.end(),
            offset,
            [](std::size_t value, detail::BoundaryFragment const& fragment)
            {
                return value < fragment.utf16Start;
            });
        if (found != projection.fragments.begin()) --found;
        auto const localUtf16 = offset >= found->utf16Start
            ? (std::min)(offset - found->utf16Start, found->utf16Length)
            : 0u;
        auto const localOffset = static_cast<std::size_t>(std::lower_bound(
            found->codepointToUtf16.begin(),
            found->codepointToUtf16.end(),
            localUtf16) - found->codepointToUtf16.begin());
        return {found->containerId, (std::min)(localOffset, found->codepointLength), affinity};
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

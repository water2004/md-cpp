#include "pch.h"
#include "editor/session/EditorSession.h"

import folia.core.serializer;
import folia.core.utf;

namespace winrt::Folia
{
    namespace
    {
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
            if (!core_->editor.undo()) return false;
        }
        else if (command.kind == folia::CommandKind::Redo)
        {
            if (!core_->editor.redo()) return false;
        }
        else if (command.kind == folia::CommandKind::SelectAll)
        {
            return core_->editor.execute_command(command);
        }
        else if (!core_->editor.execute_command(command))
        {
            return false;
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
}

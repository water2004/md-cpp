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
    void detail::BoundaryOffsetIndex::Reset(std::vector<std::size_t> const& spans)
    {
        tree.assign(spans.size() + 1, 0);
        for (std::size_t index = 0; index < spans.size(); ++index)
        {
            for (auto cursor = index + 1; cursor < tree.size(); cursor += cursor & (~cursor + 1))
            {
                tree[cursor] += spans[index];
            }
        }
    }

    bool detail::BoundaryOffsetIndex::CanAdd(std::size_t index, std::ptrdiff_t delta) const
    {
        if (index + 1 >= tree.size()) return false;
        if (delta >= 0) return true;
        auto const span = Prefix(index + 1) - Prefix(index);
        return span >= static_cast<std::size_t>(-delta);
    }

    bool detail::BoundaryOffsetIndex::Add(std::size_t index, std::ptrdiff_t delta)
    {
        if (!CanAdd(index, delta)) return false;
        for (auto cursor = index + 1; cursor < tree.size(); cursor += cursor & (~cursor + 1))
        {
            if (delta < 0) tree[cursor] -= static_cast<std::size_t>(-delta);
            else tree[cursor] += static_cast<std::size_t>(delta);
        }
        return true;
    }

    std::size_t detail::BoundaryOffsetIndex::Prefix(std::size_t count) const
    {
        count = (std::min)(count, tree.empty() ? 0u : tree.size() - 1);
        std::size_t result = 0;
        for (auto cursor = count; cursor > 0; cursor -= cursor & (~cursor + 1))
        {
            result += tree[cursor];
        }
        return result;
    }

    std::size_t detail::BoundaryOffsetIndex::Find(std::size_t offset) const
    {
        auto const count = tree.empty() ? 0u : tree.size() - 1;
        if (count == 0) return 0;
        std::size_t index = 0;
        std::size_t prefix = 0;
        std::size_t step = 1;
        while ((step << 1) <= count) step <<= 1;
        for (; step > 0; step >>= 1)
        {
            auto const next = index + step;
            if (next <= count && prefix + tree[next] <= offset)
            {
                index = next;
                prefix += tree[next];
            }
        }
        return (std::min)(index, count - 1);
    }

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

        bool IsHighSurrogate(wchar_t value)
        {
            return value >= 0xd800 && value <= 0xdbff;
        }

        bool IsLowSurrogate(wchar_t value)
        {
            return value >= 0xdc00 && value <= 0xdfff;
        }

        std::size_t Utf16OffsetForCodepoint(std::wstring_view text, std::size_t codepointOffset)
        {
            std::size_t utf16Offset = 0;
            std::size_t codepoints = 0;
            while (utf16Offset < text.size() && codepoints < codepointOffset)
            {
                if (IsHighSurrogate(text[utf16Offset])
                    && utf16Offset + 1 < text.size()
                    && IsLowSurrogate(text[utf16Offset + 1]))
                {
                    utf16Offset += 2;
                }
                else
                {
                    ++utf16Offset;
                }
                ++codepoints;
            }
            return utf16Offset;
        }

        std::size_t CodepointOffsetForUtf16(std::wstring_view text, std::size_t utf16Offset)
        {
            utf16Offset = (std::min)(utf16Offset, text.size());
            std::size_t cursor = 0;
            std::size_t codepoints = 0;
            while (cursor < utf16Offset)
            {
                if (IsHighSurrogate(text[cursor])
                    && cursor + 1 < text.size()
                    && IsLowSurrogate(text[cursor + 1]))
                {
                    cursor += 2;
                }
                else
                {
                    ++cursor;
                }
                ++codepoints;
            }
            return codepoints;
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
        text_ = {};
        InvalidateBoundaryProjection();
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
            auto const fragmentUtf16Start = projection.utf16Offsets.Prefix(index);
            if (!edit.range.valid_for(fragment.codepointLength)
                || fragmentUtf16Start + fragment.utf16Length > projection.utf16.size())
            {
                return false;
            }

            auto const fragmentUtf16 = std::wstring_view{projection.utf16}.substr(
                fragmentUtf16Start, fragment.utf16Length);
            auto const globalUtf16Start = fragmentUtf16Start
                + Utf16OffsetForCodepoint(fragmentUtf16, edit.range.start);
            auto const globalUtf16End = fragmentUtf16Start
                + Utf16OffsetForCodepoint(fragmentUtf16, edit.range.end);
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
            auto const codepointDelta = static_cast<std::ptrdiff_t>(edit.replacement.size())
                - static_cast<std::ptrdiff_t>(edit.range.length());
            auto const utf16Delta = static_cast<std::ptrdiff_t>(replacementUtf16.size())
                - static_cast<std::ptrdiff_t>(globalUtf16End - globalUtf16Start);
            auto nextCodepointLength = fragment.codepointLength;
            auto nextUtf16Length = fragment.utf16Length;
            if (!AddDelta(nextCodepointLength, codepointDelta)
                || !AddDelta(nextUtf16Length, utf16Delta)
                || !projection.codepointOffsets.CanAdd(index, codepointDelta)
                || !projection.utf16Offsets.CanAdd(index, utf16Delta))
            {
                return false;
            }
            projection.utf16.replace(
                globalUtf16Start,
                globalUtf16End - globalUtf16Start,
                replacementUtf16);
            fragment.codepointLength = nextCodepointLength;
            fragment.utf16Length = nextUtf16Length;
            if (!projection.codepointOffsets.Add(index, codepointDelta)
                || !projection.utf16Offsets.Add(index, utf16Delta)) return false;
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
            auto const index = found->second;
            auto const& fragment = projection.fragments[index];
            auto const fragmentStart = projection.utf16Offsets.Prefix(index);
            auto source = core_->editor.editable_source(edit.container_id);
            auto sourceUtf16 = source ? BoundaryWide(*source) : std::wstring{};
            if (!source || source->size() != fragment.codepointLength
                || sourceUtf16.size() != fragment.utf16Length
                || std::wstring_view{projection.utf16}.substr(
                    fragmentStart, fragment.utf16Length) != sourceUtf16)
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
        std::vector<std::size_t> codepointSpans;
        std::vector<std::size_t> utf16Spans;
        if (core_->sourceEditor)
        {
            auto const& source = core_->sourceEditor->source();
            auto const& lines = core_->sourceEditor->lines();
            projection.utf16 = BoundaryWide(source);
            projection.fragments.reserve(lines.size());
            codepointSpans.reserve(lines.size());
            utf16Spans.reserve(lines.size());
            for (std::size_t index = 0; index < lines.size(); ++index)
            {
                auto const& line = lines[index];
                auto const nextStart = index + 1 < lines.size()
                    ? lines[index + 1].source_start
                    : source.size();
                auto const codepointSpan = nextStart - line.source_start;
                auto const lineUtf16Length = BoundaryWide(line.text).size();
                auto const utf16Span = BoundaryWide(std::u32string_view{source}.substr(
                    line.source_start, codepointSpan)).size();
                projection.fragmentIndex.emplace(line.id.v, projection.fragments.size());
                projection.fragments.push_back({line.id, line.text.size(), lineUtf16Length});
                codepointSpans.push_back(codepointSpan);
                utf16Spans.push_back(utf16Span);
            }
        }
        else
        {
            auto fragments = elmd::document_text_fragments(core_->editor.document());
            projection.fragments.reserve(fragments.size());
            codepointSpans.reserve(fragments.size());
            utf16Spans.reserve(fragments.size());
            for (std::size_t index = 0; index < fragments.size(); ++index)
            {
                if (index) projection.utf16.push_back(L'\n');
                auto fragmentUtf16 = BoundaryWide(fragments[index].text);
                projection.fragmentIndex.emplace(fragments[index].container_id.v, projection.fragments.size());
                projection.fragments.push_back({
                    fragments[index].container_id,
                    fragments[index].text.size(),
                    fragmentUtf16.size(),
                });
                projection.utf16 += fragmentUtf16;
                auto const separator = index + 1 < fragments.size() ? 1u : 0u;
                codepointSpans.push_back(fragments[index].text.size() + separator);
                utf16Spans.push_back(fragmentUtf16.size() + separator);
            }
        }

        projection.codepointOffsets.Reset(codepointSpans);
        projection.utf16Offsets.Reset(utf16Spans);
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

    std::size_t EditorSession::CharacterCount() const
    {
        auto const& projection = BoundaryProjection();
        return projection.codepointOffsets.Prefix(projection.fragments.size());
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

    std::size_t EditorSession::AcpOffset(elmd::TextPosition position) const
    {
        auto const& projection = BoundaryProjection();
        auto found = projection.fragmentIndex.find(position.container_id.v);
        if (found == projection.fragmentIndex.end()) return 0;
        auto const index = found->second;
        auto const& fragment = projection.fragments[index];
        auto const codepointOffset = (std::min)(position.source_offset, fragment.codepointLength);
        auto const utf16Start = projection.utf16Offsets.Prefix(index);
        if (utf16Start + fragment.utf16Length > projection.utf16.size()) return utf16Start;
        auto const fragmentUtf16 = std::wstring_view{projection.utf16}.substr(
            utf16Start, fragment.utf16Length);
        return utf16Start + Utf16OffsetForCodepoint(fragmentUtf16, codepointOffset);
    }

    elmd::TextPosition EditorSession::PositionFromAcp(std::size_t offset, elmd::TextAffinity affinity) const
    {
        auto const& projection = BoundaryProjection();
        if (projection.fragments.empty()) return {};
        offset = (std::min)(offset, projection.utf16.size());
        auto const index = projection.utf16Offsets.Find(offset);
        auto const& fragment = projection.fragments[index];
        auto const utf16Start = projection.utf16Offsets.Prefix(index);
        auto const localUtf16 = offset >= utf16Start
            ? (std::min)(offset - utf16Start, fragment.utf16Length)
            : 0u;
        auto const fragmentUtf16 = std::wstring_view{projection.utf16}.substr(
            utf16Start, fragment.utf16Length);
        auto const localOffset = CodepointOffsetForUtf16(fragmentUtf16, localUtf16);
        return {fragment.containerId, (std::min)(localOffset, fragment.codepointLength), affinity};
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

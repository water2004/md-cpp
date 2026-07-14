#include "pch.h"
#include "EditorSession.h"

import elmd.core.editor;
import elmd.core.document_text;
import elmd.core.render_builder;
import elmd.core.render_model;
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
        core_->editor = elmd::Editor(winrt::to_string(text_));
        RebuildRenderModel();
    }

    void EditorSession::RebuildRenderModel()
    {
        core_->renderModel = elmd::build_render_model(core_->editor.document(), core_->editor.outline());
    }

    bool EditorSession::ExecuteCommand(elmd::Command const& command)
    {
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
        return !core_->editor.selection().is_caret();
    }

    std::string EditorSession::SelectedTextUtf8() const
    {
        return elmd::cps_to_utf8(core_->editor.selected_markdown_cps());
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
        return core_ ? winrt::to_hstring(core_->editor.markdown_utf8()) : text_;
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
        return BoundaryWide(TextStoreProjection(core_->editor.document()).text);
    }

    std::size_t EditorSession::AcpLength() const
    {
        return BoundaryTextUtf16().size();
    }

    std::u32string EditorSession::TextView() const
    {
        return TextStoreProjection(core_->editor.document()).text;
    }

    std::optional<std::u32string> EditorSession::EditableSource(elmd::NodeId id) const
    {
        return core_->editor.editable_source(id);
    }

    elmd::TextSelection EditorSession::Selection() const
    {
        return core_->editor.selection();
    }

    std::size_t EditorSession::AcpOffset(elmd::TextPosition position) const
    {
        TextStoreProjection projection(core_->editor.document());
        auto codepointOffset = projection.CodepointOffset(position).value_or(0);
        codepointOffset = (std::min)(codepointOffset, projection.text.size());
        return elmd::char_index_to_utf16(projection.text, codepointOffset);
    }

    elmd::TextPosition EditorSession::PositionFromAcp(std::size_t offset, elmd::TextAffinity affinity) const
    {
        TextStoreProjection projection(core_->editor.document());
        auto codepointOffset = elmd::utf16_to_char_index(projection.text, offset);
        return projection.Position(codepointOffset, affinity).value_or(elmd::TextPosition{});
    }

    elmd::RenderModel const& EditorSession::RenderModel() const
    {
        return core_->renderModel;
    }

    std::wstring const& EditorSession::BaseDirectory() const
    {
        return core_->baseDirectory;
    }

    detail::EditorRenderFrame EditorSession::RenderFrame() const
    {
        return detail::EditorRenderFrame{
            core_->renderModel,
            core_->editor.selection(),
            core_->baseDirectory,
        };
    }
}

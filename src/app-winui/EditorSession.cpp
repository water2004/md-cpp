#include "pch.h"
#include "EditorSession.h"

import elmd.core.editor;
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
        core_->editor.set_selection({anchor, active});
    }

    void EditorSession::SetSelection(elmd::TextSelection selection)
    {
        core_->editor.set_selection(std::move(selection));
    }

    bool EditorSession::HasSelection() const
    {
        return !core_->editor.selection().is_caret();
    }

    std::string EditorSession::SelectedTextUtf8() const
    {
        return elmd::cps_to_utf8(core_->editor.selected_text_cps());
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
        return BoundaryWide(core_->editor.boundary_text_cps());
    }

    std::size_t EditorSession::AcpLength() const
    {
        return BoundaryTextUtf16().size();
    }

    std::u32string EditorSession::TextView() const
    {
        return core_->editor.boundary_text_cps();
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
        auto codepointOffset = core_->editor.boundary_offset(position).value_or(0);
        auto text = core_->editor.boundary_text_cps();
        codepointOffset = (std::min)(codepointOffset, text.size());
        return BoundaryWide(std::u32string_view(text).substr(0, codepointOffset)).size();
    }

    elmd::TextPosition EditorSession::PositionFromAcp(std::size_t offset, elmd::TextAffinity affinity) const
    {
        auto text = core_->editor.boundary_text_cps();
        std::size_t codepointOffset = 0;
        std::size_t utf16Offset = 0;
        while (codepointOffset < text.size() && utf16Offset < offset)
        {
            utf16Offset += text[codepointOffset] > 0xffff ? 2 : 1;
            ++codepointOffset;
        }
        return core_->editor.boundary_position(codepointOffset, affinity).value_or(elmd::TextPosition{});
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

#include "pch.h"
#include "EditorSession.h"

import elmd.core.editor;
import elmd.core.render_builder;
import elmd.core.render_model;
import elmd.core.utf;

namespace winrt::ElMd
{
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
        core_->renderModel = elmd::build_render_model(core_->editor.document(), core_->editor.text_utf8(), core_->editor.outline());
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
            SetSelection(0, core_->editor.text_cps().size());
            return true;
        }
        else
        {
            auto executed = core_->editor.execute_command(command);
            if (!executed)
            {
                return false;
            }
        }

        text_ = winrt::to_hstring(core_->editor.text_utf8());
        revision_ = core_->editor.revision();
        RebuildRenderModel();
        return true;
    }

    void EditorSession::SetSelection(std::size_t anchor, std::size_t active, elmd::TextAffinity affinity)
    {
        auto length = core_->editor.text_cps().size();
        elmd::Selection selection;
        selection.anchor = elmd::CharOffset((std::min)(anchor, length));
        selection.active = elmd::CharOffset((std::min)(active, length));
        selection.affinity = affinity;
        core_->editor.set_selection(selection);
    }

    bool EditorSession::HasSelection() const
    {
        return !core_->editor.selection().is_caret();
    }

    std::string EditorSession::SelectedTextUtf8() const
    {
        auto range = core_->editor.selection().normalized_range();
        return elmd::cps_to_utf8(core_->editor.text_range(range));
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

    std::size_t EditorSession::TextLength() const
    {
        return core_->editor.text_cps().size();
    }

    std::u32string_view EditorSession::TextView() const
    {
        return core_->editor.text_cps();
    }

    elmd::Selection EditorSession::Selection() const
    {
        return core_->editor.selection();
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
            core_->editor.text_cps(),
            core_->editor.selection(),
            core_->baseDirectory,
        };
    }
}

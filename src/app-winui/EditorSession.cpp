#include "pch.h"
#include "EditorSession.h"

import elmd.core.editor;
import elmd.core.parser;
import elmd.core.render_builder;
import elmd.core.render_model;

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
    }

    void EditorSession::SetText(winrt::hstring const& text)
    {
        text_ = text;
        ++revision_;
        RebuildCore();
    }

    void EditorSession::RebuildCore()
    {
        core_->sourceText = winrt::to_string(text_);
        core_->editor = elmd::Editor(core_->sourceText);
        auto parsed = elmd::parse_text(core_->editor.revision(), core_->sourceText);
        core_->renderModel = elmd::build_render_model(parsed.document, core_->sourceText, parsed.outline);
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
        return text_;
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

    detail::EditorSessionCore const& EditorSession::Core() const
    {
        return *core_;
    }
}

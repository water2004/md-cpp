#pragma once

#include "EditorRenderFrame.h"

import elmd.core.editor;
import elmd.core.command;
import elmd.core.render_model;

namespace winrt::ElMd
{
    namespace detail
    {
        struct EditorSessionCore
        {
            elmd::Editor editor;
            elmd::RenderModel renderModel;
            std::wstring baseDirectory;
        };
    }

    struct EditorSession
    {
        EditorSession();
        ~EditorSession();
        EditorSession(EditorSession const&) = delete;
        EditorSession& operator=(EditorSession const&) = delete;
        EditorSession(EditorSession&&) noexcept;
        EditorSession& operator=(EditorSession&&) noexcept;

        void Open(winrt::Windows::Storage::StorageFile const& file, winrt::hstring const& text);
        void SaveAs(winrt::Windows::Storage::StorageFile const& file);
        void SetText(winrt::hstring const& text);
        bool ExecuteCommand(elmd::Command const& command);
        void SetSelection(std::size_t anchor, std::size_t active, elmd::TextAffinity affinity = elmd::TextAffinity::Downstream);
        bool HasSelection() const;
        std::string SelectedTextUtf8() const;
        bool HasFile() const;
        winrt::Windows::Storage::StorageFile File() const;
        winrt::hstring Text() const;
        winrt::hstring DisplayName() const;
        winrt::hstring Path() const;
        uint64_t Revision() const;
        std::size_t TextLength() const;
        std::u32string_view TextView() const;
        elmd::Selection Selection() const;
        elmd::RenderModel const& RenderModel() const;
        std::wstring const& BaseDirectory() const;
        detail::EditorRenderFrame RenderFrame() const;

    private:
        void RebuildCore();
        void RebuildRenderModel();

        winrt::Windows::Storage::StorageFile file_{ nullptr };
        winrt::hstring text_;
        uint64_t revision_ = 0;
        std::unique_ptr<detail::EditorSessionCore> core_;
    };
}

#pragma once

#include "editor/session/EditorRenderFrame.h"

import elmd.core.editor;
import elmd.core.command;
import elmd.core.render_model;
import elmd.core.source_editor;

namespace winrt::ElMd
{
    namespace detail
    {
        struct EditorSessionCore
        {
            elmd::Editor editor;
            std::optional<elmd::SourceEditor> sourceEditor;
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
        bool IsSourceMode() const;
        bool EnterSourceMode();
        bool ExitSourceMode();
        bool ToggleSourceMode();
        bool ExecuteCommand(elmd::Command const& command);
        void SetSelection(elmd::TextPosition anchor, elmd::TextPosition active);
        void SetSelection(elmd::TextSelection selection);
        bool HasSelection() const;
        std::string SelectedTextUtf8() const;
        bool HasFile() const;
        winrt::Windows::Storage::StorageFile File() const;
        winrt::hstring Text() const;
        winrt::hstring DisplayName() const;
        winrt::hstring Path() const;
        uint64_t Revision() const;
        std::size_t AcpLength() const;
        std::wstring BoundaryTextUtf16() const;
        std::u32string TextView() const;
        std::optional<std::u32string> EditableSource(elmd::NodeId id) const;
        elmd::TextSelection Selection() const;
        std::size_t AcpOffset(elmd::TextPosition position) const;
        elmd::TextPosition PositionFromAcp(std::size_t offset, elmd::TextAffinity affinity = elmd::TextAffinity::Downstream) const;
        elmd::RenderModel const& RenderModel() const;
        std::optional<elmd::TextPosition> FootnoteDefinitionTarget(std::string_view label) const;
        std::optional<elmd::TextPosition> FirstFootnoteReferenceTarget(std::string_view label) const;
        std::string FootnotePreview(std::string_view label) const;
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

#pragma once

#include "editor/session/EditorRenderFrame.h"

import elmd.core.editor;
import elmd.core.command;
import elmd.core.render_model;
import elmd.core.source_editor;
import elmd.core.theme;

namespace winrt::ElMd
{
    namespace detail
    {
        struct BoundaryOffsetIndex
        {
            void Reset(std::vector<std::size_t> const& spans);
            bool CanAdd(std::size_t index, std::ptrdiff_t delta) const;
            bool Add(std::size_t index, std::ptrdiff_t delta);
            std::size_t Prefix(std::size_t count) const;
            std::size_t Find(std::size_t offset) const;

            std::vector<std::size_t> tree;
        };

        struct BoundaryFragment
        {
            elmd::NodeId containerId{};
            std::size_t codepointLength = 0;
            std::size_t utf16Length = 0;
            std::vector<std::size_t> codepointToUtf16;
        };

        struct BoundaryProjection
        {
            std::u32string text;
            std::wstring utf16;
            std::vector<BoundaryFragment> fragments;
            std::unordered_map<std::uint64_t, std::size_t> fragmentIndex;
            BoundaryOffsetIndex codepointOffsets;
            BoundaryOffsetIndex utf16Offsets;
        };

        struct BoundaryTextChange
        {
            std::uint64_t revisionBefore = 0;
            std::uint64_t revisionAfter = 0;
            std::size_t utf16Start = 0;
            std::size_t utf16OldLength = 0;
            std::wstring replacement;
        };

        struct EditorSessionCore
        {
            elmd::Editor editor;
            std::optional<elmd::SourceEditor> sourceEditor;
            elmd::RenderModel renderModel;
            elmd::ThemeProfile theme = elmd::default_theme_profile();
            std::wstring baseDirectory;
            mutable std::optional<BoundaryProjection> boundaryProjection;
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
        void SetTheme(elmd::ThemeProfile const& theme);
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
        std::wstring const& BoundaryTextUtf16() const;
        std::optional<detail::BoundaryTextChange> const& LastBoundaryTextChange() const;
        std::u32string const& TextView() const;
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
        void RebuildRenderModel(elmd::EditorDocumentChange const* change = nullptr);
        void InvalidateBoundaryProjection();
        bool ApplyBoundaryProjectionChange(elmd::EditorDocumentChange const& change);
        detail::BoundaryProjection const& BoundaryProjection() const;

        winrt::Windows::Storage::StorageFile file_{ nullptr };
        winrt::hstring text_;
        uint64_t revision_ = 0;
        std::unique_ptr<detail::EditorSessionCore> core_;
        std::optional<detail::BoundaryTextChange> lastBoundaryTextChange_;
    };
}

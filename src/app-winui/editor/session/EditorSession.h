#pragma once

#include "editor/session/EditorRenderFrame.h"

import elmd.core.editor;
import elmd.core.command;
import elmd.core.render_model;
import elmd.core.search;
import elmd.core.source_editor;
import elmd.core.theme;

namespace winrt::ElMd
{
    struct EditorDocumentInteraction
    {
        std::optional<std::string> link;
        std::optional<std::string> tooltip;
    };

    struct EditorSearchSummary
    {
        std::size_t matchCount = 0;
        std::optional<std::string> error;
    };

    namespace detail
    {
        struct EditorSessionCore
        {
            struct SearchTarget
            {
                std::vector<elmd::TextSpan> spans;
                std::optional<elmd::SourceRange> sourceRange;
            };

            elmd::Editor editor;
            std::optional<elmd::SourceEditor> sourceEditor;
            elmd::RenderModel renderModel;
            elmd::ThemeProfile theme = elmd::default_theme_profile();
            std::wstring baseDirectory;
            std::size_t characterCount = 0;
            std::vector<SearchTarget> searchTargets;
            std::vector<EditorSearchHighlight> searchHighlights;
            std::optional<std::size_t> activeSearchMatch;
            std::vector<elmd::RenderedSearchFragment> renderedSearchFragments;
            std::uint64_t renderedSearchRevision = (std::numeric_limits<std::uint64_t>::max)();
        };
    }

    struct EditorSession
    {
        using LoadProgress = std::function<void(std::size_t consumed, std::size_t total)>;

        EditorSession();
        ~EditorSession();
        EditorSession(EditorSession const&) = delete;
        EditorSession& operator=(EditorSession const&) = delete;
        EditorSession(EditorSession&&) noexcept;
        EditorSession& operator=(EditorSession&&) noexcept;

        void Open(
            winrt::Windows::Storage::StorageFile const& file,
            winrt::hstring const& text,
            LoadProgress progress = {});
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
        std::size_t CharacterCount() const;
        std::wstring TextInputTextUtf16(elmd::NodeId containerId) const;
        std::size_t TextInputAcpOffset(elmd::TextPosition position) const;
        elmd::TextPosition TextInputPositionFromAcp(
            elmd::NodeId containerId,
            std::size_t offset,
            elmd::TextAffinity affinity = elmd::TextAffinity::Downstream) const;
        std::optional<std::u32string> EditableSource(elmd::NodeId id) const;
        std::optional<EditorDocumentInteraction> InteractionAt(elmd::TextPosition position) const;
        elmd::TextSelection Selection() const;
        elmd::RenderModel const& RenderModel() const;
        elmd::RenderModel BuildPrintRenderModel() const;
        std::optional<elmd::TextPosition> FootnoteDefinitionTarget(std::string_view label) const;
        std::optional<elmd::TextPosition> FirstFootnoteReferenceTarget(std::string_view label) const;
        std::string FootnotePreview(std::string_view label) const;
        std::wstring const& BaseDirectory() const;
        EditorSearchSummary Search(
            std::u32string_view query,
            elmd::SearchOptions options = {});
        bool ActivateSearchMatch(std::size_t index);
        bool ReplaceSearchMatch(
            std::size_t index,
            std::u32string_view query,
            std::u32string_view replacement,
            elmd::SearchOptions options = {});
        bool ReplaceAllSearchMatches(
            std::u32string_view query,
            std::u32string_view replacement,
            elmd::SearchOptions options = {});
        void ClearSearch();
        detail::EditorRenderFrame RenderFrame();

    private:
        void RebuildCore(LoadProgress progress = {});
        void RebuildRenderModel(elmd::EditorDocumentChange const* change = nullptr);
        bool ShouldVirtualizeRenderModel() const;
        void MaterializeRenderBlocks(std::size_t begin, std::size_t end);
        void ReleaseRenderBlocksOutside(std::size_t begin, std::size_t end);
        void RefreshCharacterCount();
        std::optional<std::u32string_view> TextInputSourceView(elmd::NodeId containerId) const;
        void RebuildSearchHighlights();
        bool CommitRenderedSearchReplacement(std::span<const elmd::RenderedSearchMatch> matches);
        std::span<const elmd::RenderedSearchFragment> RenderedSearchFragments();

        winrt::Windows::Storage::StorageFile file_{ nullptr };
        winrt::hstring text_;
        uint64_t revision_ = 0;
        std::unique_ptr<detail::EditorSessionCore> core_;
    };

}

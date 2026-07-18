#pragma once

#include "editor/session/EditorRenderFrame.h"

import folia.core.editor;
import folia.core.command;
import folia.core.render_model;
import folia.core.search;
import folia.core.latex_completion;
import folia.core.source_editor;
import folia.core.theme;
import folia.platform.editor_shortcuts;

namespace winrt::Folia
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
                std::vector<folia::TextSpan> spans;
                std::optional<folia::SourceRange> sourceRange;
            };

            folia::Editor editor;
            std::optional<folia::SourceEditor> sourceEditor;
            folia::RenderModel renderModel;
            folia::ThemeProfile theme = folia::default_theme_profile();
            std::wstring baseDirectory;
            std::size_t characterCount = 0;
            std::vector<SearchTarget> searchTargets;
            std::vector<EditorSearchHighlight> searchHighlights;
            std::optional<std::size_t> activeSearchMatch;
            std::vector<folia::RenderedSearchFragment> renderedSearchFragments;
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
        void SetTheme(folia::ThemeProfile const& theme);
        bool IsSourceMode() const;
        bool EnterSourceMode();
        bool ExitSourceMode();
        bool ToggleSourceMode();
        bool ExecuteCommand(folia::Command const& command);
        void SetSelection(folia::TextPosition anchor, folia::TextPosition active);
        void SetSelection(folia::TextSelection selection);
        bool HasSelection() const;
        std::u32string SelectedSource() const;
        std::string SelectedTextUtf8() const;
        bool HasFile() const;
        winrt::Windows::Storage::StorageFile File() const;
        winrt::hstring Text() const;
        winrt::hstring DisplayName() const;
        winrt::hstring Path() const;
        uint64_t Revision() const;
        std::size_t CharacterCount() const;
        std::wstring TextInputTextUtf16(folia::NodeId containerId) const;
        std::size_t TextInputAcpOffset(folia::TextPosition position) const;
        folia::TextPosition TextInputPositionFromAcp(
            folia::NodeId containerId,
            std::size_t offset,
            folia::TextAffinity affinity = folia::TextAffinity::Downstream) const;
        std::optional<std::u32string> EditableSource(folia::NodeId id) const;
        std::optional<EditorDocumentInteraction> InteractionAt(folia::TextPosition position) const;
        folia::TextSelection Selection() const;
        folia::platform::editor::EditorShortcutScope ShortcutScope() const;
        std::optional<folia::LatexCommandPrefix> LatexCommandPrefixAtCaret() const;
        folia::RenderModel const& RenderModel() const;
        folia::RenderModel BuildPrintRenderModel() const;
        std::optional<folia::TextPosition> FootnoteDefinitionTarget(std::string_view label) const;
        std::optional<folia::TextPosition> FirstFootnoteReferenceTarget(std::string_view label) const;
        std::string FootnotePreview(std::string_view label) const;
        std::wstring const& BaseDirectory() const;
        EditorSearchSummary Search(
            std::u32string_view query,
            folia::SearchOptions options = {});
        bool ActivateSearchMatch(std::size_t index);
        bool ReplaceSearchMatch(
            std::size_t index,
            std::u32string_view query,
            std::u32string_view replacement,
            folia::SearchOptions options = {});
        bool ReplaceAllSearchMatches(
            std::u32string_view query,
            std::u32string_view replacement,
            folia::SearchOptions options = {});
        void ClearSearch();
        detail::EditorRenderFrame RenderFrame();

    private:
        void RebuildCore(LoadProgress progress = {});
        void RebuildRenderModel(folia::EditorDocumentChange const* change = nullptr);
        bool ShouldVirtualizeRenderModel() const;
        void MaterializeRenderBlocks(std::size_t begin, std::size_t end);
        void ReleaseRenderBlocksOutside(std::size_t begin, std::size_t end);
        void RefreshCharacterCount();
        std::optional<std::u32string_view> TextInputSourceView(folia::NodeId containerId) const;
        void RebuildSearchHighlights();
        bool CommitRenderedSearchReplacement(std::span<const folia::RenderedSearchMatch> matches);
        std::span<const folia::RenderedSearchFragment> RenderedSearchFragments();

        winrt::Windows::Storage::StorageFile file_{ nullptr };
        winrt::hstring text_;
        uint64_t revision_ = 0;
        std::unique_ptr<detail::EditorSessionCore> core_;
    };

}

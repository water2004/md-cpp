#pragma once

#include "editor/session/EditorRenderFrame.h"

import elmd.core.editor;
import elmd.core.command;
import elmd.core.render_model;
import elmd.core.source_editor;
import elmd.core.theme;

namespace winrt::ElMd
{
    struct EditorDocumentInteraction
    {
        std::optional<std::string> link;
        std::optional<std::string> tooltip;
    };

    namespace detail
    {
        struct EditorSessionCore
        {
            elmd::Editor editor;
            std::optional<elmd::SourceEditor> sourceEditor;
            elmd::RenderModel renderModel;
            elmd::ThemeProfile theme = elmd::default_theme_profile();
            std::wstring baseDirectory;
            std::size_t characterCount = 0;
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
        detail::EditorRenderFrame RenderFrame();

    private:
        void RebuildCore(LoadProgress progress = {});
        void RebuildRenderModel(elmd::EditorDocumentChange const* change = nullptr);
        bool ShouldVirtualizeRenderModel() const;
        void MaterializeRenderBlocks(std::size_t begin, std::size_t end);
        void ReleaseRenderBlocksOutside(std::size_t begin, std::size_t end);
        void RefreshCharacterCount();
        std::optional<std::u32string_view> TextInputSourceView(elmd::NodeId containerId) const;

        winrt::Windows::Storage::StorageFile file_{ nullptr };
        winrt::hstring text_;
        uint64_t revision_ = 0;
        std::unique_ptr<detail::EditorSessionCore> core_;
    };
}

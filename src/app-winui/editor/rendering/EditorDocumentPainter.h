#pragma once

import elmd.platform.editor_interaction;

#include "EditorContentPreparation.h"
#include "EditorRenderResources.h"
#include "EditorStyleSheet.h"
#include "editor/session/EditorRenderFrame.h"

namespace winrt::ElMd
{
    using elmd::platform::editor::EditorInteractionMap;

    class EditorDocumentPainter
    {
    public:
        struct PositionedMath
        {
            DisplayInlineText::MathOverlay const* overlay = nullptr;
            float localX = 0.0f;
            float localTop = 0.0f;
        };

        EditorDocumentPainter(
            EditorRenderResources& resources,
            EditorStyleSheet const& styleSheet,
            EditorInteractionMap& interactionMap,
            TreeSitterHighlighter& treeSitter,
            std::optional<D2D1_POINT_2F> pointerPosition,
            bool printMode,
            float documentRight,
            elmd::TextSelection selection,
            std::span<const detail::EditorSearchHighlight> searchHighlights,
            std::unordered_map<std::uint64_t, std::size_t> const& editableIndex);

        void DrawTaskCheckboxes(
            IDWriteTextLayout* layout,
            D2D1_POINT_2F origin,
            std::vector<DisplayInlineText::TaskCheckboxOverlay> const& overlays);
        void RegisterFootnotes(
            IDWriteTextLayout* layout,
            D2D1_POINT_2F origin,
            std::vector<DisplayInlineText::FootnoteOverlay> const& overlays);
        void DrawSelection(
            IDWriteTextLayout* layout,
            D2D1_POINT_2F origin,
            EditorDisplayMapping const& mapping);
        void DrawSearchHighlights(
            IDWriteTextLayout* layout,
            D2D1_POINT_2F origin,
            EditorDisplayMapping const& mapping);
        void ApplyNestedCodeHighlights(
            DisplayInlineText& display,
            elmd::RenderBlock const& parent);
        void DrawFlowDecorations(
            IDWriteTextLayout* layout,
            D2D1_POINT_2F origin,
            elmd::RenderBlock const& parent,
            DisplayInlineText const& display);

        static std::vector<PositionedMath> PositionMath(
            IDWriteTextLayout* layout,
            std::vector<DisplayInlineText::MathOverlay> const& overlays,
            float width);

    private:
        bool PositionLess(elmd::TextPosition left, elmd::TextPosition right) const;
        bool Selected(elmd::TextPosition position) const;

        EditorRenderResources& resources;
        EditorStyleSheet const& styleSheet;
        EditorInteractionMap& interactionMap;
        TreeSitterHighlighter& treeSitter;
        std::optional<D2D1_POINT_2F> pointerPosition;
        bool printMode = false;
        float documentRight = 0.0f;
        elmd::TextSelection selection;
        std::span<const detail::EditorSearchHighlight> searchHighlights;
        std::unordered_map<std::uint64_t, std::size_t> const& editableIndex;
        elmd::TextPosition selectionStart;
        elmd::TextPosition selectionEnd;
    };
}

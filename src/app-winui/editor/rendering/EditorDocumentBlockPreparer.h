#pragma once

import folia.core.render_model;
import folia.core.text_edit;

#include "editor/rendering/EditorDocumentPainter.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "editor/rendering/EditorRenderCache.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/rendering/EditorTextLayoutEngine.h"
#include "editor/session/EditorRenderFrame.h"
#include "media/MathJaxRenderer.h"
#include "media/SvgNormalizer.h"
#include "media/TreeSitterHighlighter.h"

namespace winrt::Folia
{
    // Builds one prepared block at a time. The object is stack-owned by a
    // document preparation pass so its syntax cache never outlives a frame.
    class EditorDocumentBlockPreparer final
    {
    public:
        EditorDocumentBlockPreparer(
            detail::EditorRenderFrame const& frame,
            EditorRenderResources& resources,
            EditorRenderCache& renderCache,
            EditorStyleSheet const& styleSheet,
            EditorTextLayoutEngine& textLayoutEngine,
            EditorInlineImageRenderer& inlineImages,
            EditorDocumentPainter& documentPainter,
            MathJaxRenderer& mathJax,
            SvgNormalizer& svgNormalizer,
            TreeSitterHighlighter& treeSitter,
            folia::TextPosition caret,
            float documentWidth,
            bool mathSvgSupported,
            std::uint64_t embeddedGeneration,
            std::uint64_t remoteImageGeneration);

        void InitializeMetadata(
            EditorPreparedDocument::Block& prepared,
            folia::RenderBlock const& block) const;
        void InitializeContentMetadata(
            EditorPreparedDocument::Block& prepared,
            folia::RenderBlock const& block) const;
        float EstimateHeight(folia::RenderBlock const& block);
        EditorPreparedDocument::Block Prepare(
            folia::RenderBlock const& block,
            bool requestEmbedded,
            bool highPriority);

        static std::vector<std::string> ImageSources(
            EditorPreparedDocument::Block const& prepared);

    private:
        DisplayInlineText BuildDisplay(
            folia::RenderBlock const& block,
            float width,
            bool requestEmbedded,
            bool highPriority);

        detail::EditorRenderFrame const& frame;
        EditorRenderResources& resources;
        EditorRenderCache& renderCache;
        EditorStyleSheet const& styleSheet;
        EditorTextLayoutEngine& textLayoutEngine;
        EditorInlineImageRenderer& inlineImages;
        EditorDocumentPainter& documentPainter;
        MathJaxRenderer& mathJax;
        SvgNormalizer& svgNormalizer;
        TreeSitterHighlighter& treeSitter;
        folia::TextPosition caret;
        float documentWidth = 0.0f;
        bool mathSvgSupported = false;
        std::uint64_t embeddedGeneration = 0;
        std::uint64_t remoteImageGeneration = 0;
        std::unordered_map<void const*, std::vector<SyntaxHighlightRange>> sourceCodeHighlights;
    };
}

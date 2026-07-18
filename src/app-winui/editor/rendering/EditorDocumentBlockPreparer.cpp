#include "pch.h"
#include "editor/rendering/EditorDocumentBlockPreparer.h"

import folia.core.utf;

namespace winrt::Folia
{
    namespace
    {
        bool InlineItemsContain(
            std::vector<folia::InlineRenderItem> const& items,
            folia::InlineRenderItem::Kind kind)
        {
            for (auto const& item : items)
            {
                if (item.kind == kind
                    || InlineItemsContain(item.special().semantic().children, kind)) return true;
            }
            return false;
        }

        bool RenderBlockContainsMath(folia::RenderBlock const& block)
        {
            if (block.kind == folia::RenderBlockKind::Math
                || InlineItemsContain(block.inline_items, folia::InlineRenderItem::Kind::Math)) return true;
            for (auto const& cell : block.special().table_cells)
                if (InlineItemsContain(cell, folia::InlineRenderItem::Kind::Math)) return true;
            for (auto const& child : block.child_blocks)
                if (RenderBlockContainsMath(child)) return true;
            return false;
        }

        bool RenderBlockContainsImage(folia::RenderBlock const& block)
        {
            if (block.kind == folia::RenderBlockKind::Image
                || InlineItemsContain(block.inline_items, folia::InlineRenderItem::Kind::Image)) return true;
            for (auto const& cell : block.special().table_cells)
                if (InlineItemsContain(cell, folia::InlineRenderItem::Kind::Image)) return true;
            for (auto const& child : block.child_blocks)
                if (RenderBlockContainsImage(child)) return true;
            return false;
        }

        void CollectInlineOwners(
            std::vector<folia::InlineRenderItem> const& items,
            std::unordered_set<std::uint64_t>& seen,
            std::vector<folia::NodeId>& owners)
        {
            for (auto const& item : items)
            {
                auto owner = item.source_span.container_id;
                if (owner.v != 0 && seen.insert(owner.v).second) owners.push_back(owner);
                CollectInlineOwners(item.special().semantic().children, seen, owners);
            }
        }

        void CollectRenderOwners(
            folia::RenderBlock const& block,
            std::unordered_set<std::uint64_t>& seen,
            std::vector<folia::NodeId>& owners)
        {
            auto add = [&](folia::NodeId owner)
            {
                if (owner.v != 0 && seen.insert(owner.v).second) owners.push_back(owner);
            };
            add(block.id);
            add(block.source_span.container_id);
            add(block.content_span.container_id);
            CollectInlineOwners(block.inline_items, seen, owners);
            for (auto const& cell : block.special().table_cells)
                CollectInlineOwners(cell, seen, owners);
            for (auto const& span : block.special().table_cell_spans) add(span.container_id);
            for (auto const& child : block.child_blocks)
                CollectRenderOwners(child, seen, owners);
        }
    }

    EditorDocumentBlockPreparer::EditorDocumentBlockPreparer(
        detail::EditorRenderFrame const& valueFrame,
        EditorRenderResources& valueResources,
        EditorRenderCache& valueRenderCache,
        EditorStyleSheet const& valueStyleSheet,
        EditorTextLayoutEngine& valueTextLayoutEngine,
        EditorInlineImageRenderer& valueInlineImages,
        EditorDocumentPainter& valueDocumentPainter,
        MathJaxRenderer& valueMathJax,
        SvgNormalizer& valueSvgNormalizer,
        TreeSitterHighlighter& valueTreeSitter,
        folia::TextPosition valueCaret,
        float valueDocumentWidth,
        bool valueMathSvgSupported,
        std::uint64_t valueEmbeddedGeneration,
        std::uint64_t valueRemoteImageGeneration)
        : frame(valueFrame),
          resources(valueResources),
          renderCache(valueRenderCache),
          styleSheet(valueStyleSheet),
          textLayoutEngine(valueTextLayoutEngine),
          inlineImages(valueInlineImages),
          documentPainter(valueDocumentPainter),
          mathJax(valueMathJax),
          svgNormalizer(valueSvgNormalizer),
          treeSitter(valueTreeSitter),
          caret(valueCaret),
          documentWidth(valueDocumentWidth),
          mathSvgSupported(valueMathSvgSupported),
          embeddedGeneration(valueEmbeddedGeneration),
          remoteImageGeneration(valueRemoteImageGeneration)
    {
    }

    void EditorDocumentBlockPreparer::InitializeMetadata(
        EditorPreparedDocument::Block& prepared,
        folia::RenderBlock const& block) const
    {
        prepared.sourceId = block.id;
        prepared.presentationKey = block.presentation_key;
        prepared.sourceMode = block.source_mode;
        prepared.code = block.kind == folia::RenderBlockKind::Code
            || block.kind == folia::RenderBlockKind::Frontmatter
            || block.kind == folia::RenderBlockKind::Unsupported;
    }

    void EditorDocumentBlockPreparer::InitializeContentMetadata(
        EditorPreparedDocument::Block& prepared,
        folia::RenderBlock const& block) const
    {
        prepared.containsMath = RenderBlockContainsMath(block);
        prepared.containsImage = RenderBlockContainsImage(block);
        std::unordered_set<std::uint64_t> owners;
        CollectRenderOwners(block, owners, prepared.owners);
    }

    float EditorDocumentBlockPreparer::EstimateHeight(folia::RenderBlock const& block)
    {
        auto const& special = block.special();
        auto flowContainer = !block.inline_items.empty()
            && (block.kind == folia::RenderBlockKind::Quote
                || block.kind == folia::RenderBlockKind::Callout);
        auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
        auto paddingBottom = flowContainer ? 0.0f : block.block_style.padding_bottom;
        auto paddingLeft = flowContainer ? 0.0f : block.block_style.padding_left;
        auto paddingRight = flowContainer ? 0.0f : block.block_style.padding_right;
        auto contentWidth = (std::max)(1.0f, documentWidth - paddingLeft - paddingRight);
        if (block.kind == folia::RenderBlockKind::ThematicBreak) return 40.0f;
        if (block.kind == folia::RenderBlockKind::Blank)
            return styleSheet.body.lineHeight + paddingTop + paddingBottom;
        if (block.source_code)
            return styleSheet.code.lineHeight + paddingTop + paddingBottom;
        if (block.kind == folia::RenderBlockKind::Code
            || block.kind == folia::RenderBlockKind::Frontmatter
            || block.kind == folia::RenderBlockKind::Unsupported)
        {
            auto lines = (std::max)(std::size_t{1}, special.line_count);
            if (special.line_count == 0)
            {
                auto const& source = special.code_text.empty() ? special.raw_source : special.code_text;
                lines = 1 + static_cast<std::size_t>(std::ranges::count(source, U'\n'));
            }
            return static_cast<float>(lines) * styleSheet.code.lineHeight
                + paddingTop + paddingBottom;
        }
        if (block.kind == folia::RenderBlockKind::Math)
            return styleSheet.body.lineHeight * 2.0f + paddingTop + paddingBottom;
        if (block.kind == folia::RenderBlockKind::Table)
            return static_cast<float>((std::max)(
                std::size_t{1},
                special.row_count != 0
                    ? special.row_count
                    : static_cast<std::size_t>(block.estimated_line_breaks)))
                * (styleSheet.body.lineHeight + 16.0f) + paddingTop + paddingBottom;
        if (block.kind == folia::RenderBlockKind::Image)
        {
            auto requestedWidth = ResolveImageDimension(special.image_width, contentWidth);
            auto requestedHeight = ResolveImageDimension(special.image_height);
            auto width = requestedWidth.value_or(0.0f);
            auto height = requestedHeight.value_or(0.0f);
            if ((width <= 0.0f || height <= 0.0f) && !special.src.empty())
            {
                if (auto dimensions = renderCache.ProbeImageDimensions(
                        resources, frame.baseDirectory, special.src))
                {
                    width = requestedWidth.value_or(dimensions->width);
                    height = requestedHeight.value_or(dimensions->height);
                    if (requestedWidth && !requestedHeight)
                        height = dimensions->height * width / dimensions->width;
                    if (!requestedWidth && requestedHeight)
                        width = dimensions->width * height / dimensions->height;
                }
            }
            if (width <= 0.0f) width = contentWidth * 0.5f;
            if (height <= 0.0f) height = styleSheet.body.lineHeight;
            auto scale = (std::min)(1.0f, (std::min)(
                (std::max)(48.0f, contentWidth * 0.75f) / width,
                240.0f / height));
            auto caption = special.alt.empty() ? 0.0f : styleSheet.body.lineHeight + 4.0f;
            return (std::max)(styleSheet.body.lineHeight, height * scale)
                + caption + paddingTop + paddingBottom;
        }

        auto level = block.text_heading_level;
        auto font = level == 0 ? styleSheet.body
            : level <= 1 ? styleSheet.heading1
            : level == 2 ? styleSheet.heading2
            : styleSheet.heading3;
        for (auto const& source : special.inline_image_sources)
            renderCache.ProbeImageDimensions(resources, frame.baseDirectory, source);
        auto averageAdvance = (std::max)(1.0f, font.size * 0.72f);
        auto charactersPerLine = (std::max)(
            std::size_t{1},
            static_cast<std::size_t>(contentWidth / averageAdvance));
        auto wrappedLines = (static_cast<std::size_t>(block.estimated_characters)
            + charactersPerLine - 1) / charactersPerLine;
        auto lines = (std::max)(
            std::size_t{1} + static_cast<std::size_t>(block.estimated_line_breaks),
            (std::max)(std::size_t{1}, wrappedLines));
        return static_cast<float>(lines) * font.lineHeight + paddingTop + paddingBottom;
    }

    DisplayInlineText EditorDocumentBlockPreparer::BuildDisplay(
        folia::RenderBlock const& block,
        float width,
        bool requestEmbedded)
    {
        if (block.kind == folia::RenderBlockKind::Blank)
        {
            DisplayInlineText display;
            AppendGeneratedText(
                display,
                U"\u200B",
                {block.id, 0, folia::TextAffinity::Downstream},
                folia::InlineStyle::plain());
            display.displayToSource.push_back({block.id, 0, folia::TextAffinity::Downstream});
            return display;
        }
        if (block.kind == folia::RenderBlockKind::Code)
            return BuildCodeBlockText(block, caret, treeSitter);
        if (block.kind == folia::RenderBlockKind::Math)
            return BuildMathBlockText(
                block,
                caret,
                mathJax,
                svgNormalizer,
                styleSheet.textColor,
                styleSheet.body.size,
                width,
                mathSvgSupported,
                requestEmbedded);
        if (!block.inline_items.empty())
        {
            auto sourceEnd = InlineItemsEndPosition(
                block.inline_items,
                {block.id, block.content_span.source_range.end, folia::TextAffinity::Downstream});
            return BuildDisplayInlineText(
                block.inline_items,
                caret,
                sourceEnd,
                mathJax,
                svgNormalizer,
                styleSheet.textColor,
                styleSheet.body.size,
                width,
                mathSvgSupported,
                requestEmbedded);
        }

        DisplayInlineText display;
        if (block.kind == folia::RenderBlockKind::Image)
        {
            auto const& special = block.special();
            auto position = folia::TextPosition{block.id, 0, folia::TextAffinity::Downstream};
            auto start = static_cast<std::uint32_t>(display.displayToSource.size());
            AppendGeneratedText(display, U"\uFFFC", position, folia::InlineStyle::plain());
            display.imageOverlays.push_back({
                start,
                block.source_span,
                special.src,
                special.alt,
                special.image_width,
                special.image_height,
                true,
            });
        }
        else if (block.kind == folia::RenderBlockKind::Toc)
        {
            for (auto const* item : frame.renderModel.outline.flat_items())
            {
                AppendGeneratedText(
                    display,
                    folia::utf8_to_cps(item->title_plain_text) + U"\n",
                    {block.id, 0, folia::TextAffinity::Downstream},
                    folia::InlineStyle{.link = true});
            }
        }
        else
        {
            auto const& special = block.special();
            auto raw = folia::utf8_to_cps(
                special.raw.empty() ? special.reason_text : special.raw);
            AppendSourceText(
                display,
                raw,
                {block.id, {0, raw.size()}},
                folia::InlineStyle::plain(),
                false);
        }
        if (display.text.empty())
            AppendGeneratedText(
                display,
                U"\u200B",
                {block.id, 0, folia::TextAffinity::Downstream},
                folia::InlineStyle::plain());
        display.displayToSource.push_back({
            block.id,
            block.source_span.source_range.end,
            folia::TextAffinity::Downstream,
        });
        return display;
    }

    EditorPreparedDocument::Block EditorDocumentBlockPreparer::Prepare(
        folia::RenderBlock const& block,
        bool requestEmbedded)
    {
        EditorPreparedDocument::Block prepared;
        InitializeMetadata(prepared, block);
        InitializeContentMetadata(prepared, block);
        prepared.embeddedRequested = requestEmbedded;
        prepared.embeddedGeneration = embeddedGeneration;
        prepared.remoteImageGeneration = remoteImageGeneration;
        std::unordered_set<std::uint64_t> owners;
        for (auto owner : prepared.owners) owners.insert(owner.v);
        auto addOwner = [&](folia::NodeId id)
        {
            if (id.v != 0 && owners.insert(id.v).second) prepared.owners.push_back(id);
        };
        addOwner(block.id);
        addOwner(block.source_span.container_id);
        if (block.kind == folia::RenderBlockKind::Table)
        {
            prepared.table = EditorTableBlockRenderer::Prepare(
                block,
                caret,
                documentWidth,
                mathSvgSupported,
                requestEmbedded,
                resources,
                styleSheet,
                textLayoutEngine,
                inlineImages,
                mathJax,
                svgNormalizer);
            if (prepared.table)
            {
                for (auto const& span : block.special().table_cell_spans)
                    addOwner(span.container_id);
                prepared.pendingMath = prepared.table->pendingMath;
                prepared.pendingImage = prepared.table->pendingImage;
                prepared.height = prepared.table->height;
                prepared.valid = true;
                return prepared;
            }
        }
        auto flowContainer = !block.inline_items.empty()
            && (block.kind == folia::RenderBlockKind::Quote
                || block.kind == folia::RenderBlockKind::Callout);
        auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
        auto paddingBottom = flowContainer ? 0.0f : block.block_style.padding_bottom;
        auto paddingLeft = flowContainer ? 0.0f : block.block_style.padding_left;
        auto paddingRight = flowContainer ? 0.0f : block.block_style.padding_right;
        auto contentWidth = (std::max)(1.0f, documentWidth - paddingLeft - paddingRight);
        prepared.display = BuildDisplay(block, contentWidth, requestEmbedded);
        if (block.source_mode
            && block.source_code
            && block.special().language
            && !prepared.display.text.empty())
        {
            auto const* contextKey = block.source_code_context
                ? static_cast<void const*>(block.source_code_context.get())
                : static_cast<void const*>(&block);
            auto [found, inserted] = sourceCodeHighlights.try_emplace(contextKey);
            if (inserted)
            {
                auto const& context = block.source_code_context
                    ? *block.source_code_context
                    : prepared.display.text;
                found->second = treeSitter.Highlight(
                    *block.special().language,
                    folia::cps_to_utf8(context));
            }
            auto lineStart = block.source_code_context ? block.source_code_context_offset : 0;
            auto lineEnd = lineStart + prepared.display.text.size();
            for (auto const& highlight : found->second)
            {
                auto highlightStart = static_cast<std::size_t>(highlight.start);
                auto highlightEnd = highlightStart + static_cast<std::size_t>(highlight.length);
                if (highlightEnd <= lineStart || highlightStart >= lineEnd) continue;
                auto start = (std::max)(highlightStart, lineStart) - lineStart;
                auto end = (std::min)(highlightEnd, lineEnd) - lineStart;
                auto displayStart = folia::char_index_to_utf16(prepared.display.text, start);
                auto displayEnd = folia::char_index_to_utf16(prepared.display.text, end);
                if (displayEnd <= displayStart) continue;
                prepared.display.ranges.push_back({
                    static_cast<UINT32>(displayStart),
                    static_cast<UINT32>(displayEnd - displayStart),
                    folia::InlineStyle::plain(),
                    false,
                    highlight.kind,
                });
            }
        }
        documentPainter.ApplyNestedCodeHighlights(prepared.display, block);
        prepared.pendingMath = prepared.display.pendingMath;
        auto format = textLayoutEngine.FormatFor(
            prepared.code || prepared.sourceMode,
            prepared.display.ranges);
        prepared.layout = textLayoutEngine.CreateFlow(
            prepared.display,
            format,
            contentWidth,
            [&](IDWriteTextLayout* candidate, DisplayInlineText const& candidateDisplay)
            {
                prepared.images = inlineImages.Resolve(
                    candidate,
                    candidateDisplay.imageOverlays,
                    contentWidth,
                    requestEmbedded);
                prepared.pendingImage = std::ranges::any_of(
                    prepared.images,
                    [](auto const& image) { return image.pending; });
            });
        if (prepared.layout && !block.source_mode && block.block_style.text_alignment)
        {
            auto alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
            switch (*block.block_style.text_alignment)
            {
                case folia::TextAlignment::Start:
                    alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
                    break;
                case folia::TextAlignment::Center:
                    alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
                    break;
                case folia::TextAlignment::End:
                    alignment = DWRITE_TEXT_ALIGNMENT_TRAILING;
                    break;
                case folia::TextAlignment::Justify:
                    alignment = DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
                    break;
            }
            prepared.layout->SetTextAlignment(alignment);
        }
        prepared.mathPreviews.reserve(prepared.display.mathPreviews.size());
        for (auto const& preview : prepared.display.mathPreviews)
        {
            auto previewDisplay = BuildMathPreviewText(preview);
            auto previewLayout = textLayoutEngine.CreateFlow(
                previewDisplay,
                resources.textFormat.Get(),
                (std::max)(1.0f, contentWidth - 16.0f),
                {});
            auto previewHeight = textLayoutEngine.MeasureHeight(
                previewLayout.Get(),
                styleSheet.body.lineHeight);
            prepared.mathPreviews.push_back({
                std::move(previewDisplay),
                std::move(previewLayout),
                previewHeight,
            });
        }
        prepared.textHeight = textLayoutEngine.MeasureHeight(
            prepared.layout.Get(),
            textLayoutEngine.LineHeightFor(
                prepared.code || prepared.sourceMode,
                prepared.display.ranges));
        auto previewHeight = 0.0f;
        for (auto const& preview : prepared.mathPreviews)
            previewHeight += preview.height + 24.0f;
        prepared.height = prepared.textHeight + previewHeight + paddingTop + paddingBottom;
        for (auto const& position : prepared.display.displayToSource)
            addOwner(position.container_id);
        prepared.valid = true;
        return prepared;
    }

    std::vector<std::string> EditorDocumentBlockPreparer::ImageSources(
        EditorPreparedDocument::Block const& prepared)
    {
        std::vector<std::string> sources;
        sources.reserve(prepared.images.size());
        for (auto const& image : prepared.images)
            if (!image.source.empty()) sources.push_back(image.source);
        if (prepared.table)
        {
            for (auto const& cellImages : prepared.table->imageDraws)
                for (auto const& image : cellImages)
                    if (!image.source.empty()) sources.push_back(image.source);
        }
        return sources;
    }
}

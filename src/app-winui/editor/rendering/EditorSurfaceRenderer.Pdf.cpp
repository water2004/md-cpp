#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "export/EditorPdfPrintJob.h"

import folia.core.layout_plan;

namespace winrt::Folia
{
    namespace
    {
        constexpr float PdfPageWidth = 210.0f / 25.4f * 96.0f;
        constexpr float PdfPageHeight = 297.0f / 25.4f * 96.0f;
        constexpr float PdfPageMargin = 48.0f;
        constexpr float PdfContentWidth = PdfPageWidth - PdfPageMargin * 2.0f;
        constexpr float PdfContentHeight = PdfPageHeight - PdfPageMargin * 2.0f;
    }

    struct EditorSurfaceRenderer::PdfExportState
    {
        std::filesystem::path path;
        std::wstring title;
        std::unique_ptr<EditorPreparedDocument> prepared;
        EditorInteractionMap interaction;
        std::vector<D2D1_RECT_F> nonInteractive;
        float totalHeight = 0.0f;
        std::vector<folia::PrintBlockExtent> extents;
        std::size_t nextBlock = 0;
        float nextSourceTop = 0.0f;
        std::size_t completedPages = 0;
        std::size_t estimatedTotalPages = 0;
        std::unique_ptr<EditorPdfPrintJob> printJob;
        bool completed = false;

        ~PdfExportState()
        {
            printJob.reset();
            if (!completed && !path.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        }
    };

    EditorPdfExportProgress EditorSurfaceRenderer::ExportPdfStep(
        std::filesystem::path const& path,
        std::wstring const& title,
        detail::EditorRenderFrame const& frame)
    {
        if (!resources.Ready()) winrt::throw_hresult(E_UNEXPECTED);
        if (rendering || resizing || exporting)
        {
            return {
                EditorPdfExportResult::WaitingForAssets,
                pdfExportState ? pdfExportState->completedPages : 0,
                pdfExportState ? pdfExportState->estimatedTotalPages : 0,
            };
        }
        if (pdfExportState && (pdfExportState->path != path || pdfExportState->title != title))
            CancelPdfExport();
        if (!pdfExportState)
        {
            pdfExportState = std::make_shared<PdfExportState>();
            pdfExportState->path = path;
            pdfExportState->title = title;
            renderCache.ClearTextLayouts();
            renderCache.ClearSvgDocuments();
            resources.RebuildTextFormats(styleSheet);
            resources.ResetBrushes();
            resources.EnsureFrameResources(styleSheet);
        }
        auto state = pdfExportState;
        exporting = true;

        ::Microsoft::WRL::ComPtr<ID2D1Image> originalTarget;
        resources.d2dContext->GetTarget(originalTarget.GetAddressOf());
        D2D1_MATRIX_3X2_F originalTransform{};
        resources.d2dContext->GetTransform(&originalTransform);
        auto originalWidth = resources.surfaceWidthDip;
        auto originalHeight = resources.surfaceHeightDip;
        auto originalScroll = scrollState.Save();
        auto originalTotalHeight = totalDocumentHeight;
        auto originalPrepared = std::move(preparedDocument);
        auto originalInteraction = std::move(interactionMap);
        auto originalNonInteractive = std::move(nonInteractiveRegions);
        bool restored = false;

        preparedDocument = std::move(state->prepared);
        interactionMap = std::move(state->interaction);
        nonInteractiveRegions = std::move(state->nonInteractive);
        totalDocumentHeight = state->totalHeight;
        printMode = true;
        resources.surfaceWidthDip = PdfContentWidth;
        resources.surfaceHeightDip = PdfContentHeight;

        auto restore = [&]() noexcept
        {
            if (restored) return;
            restored = true;
            state->prepared = std::move(preparedDocument);
            state->interaction = std::move(interactionMap);
            state->nonInteractive = std::move(nonInteractiveRegions);
            state->totalHeight = totalDocumentHeight;
            resources.d2dContext->SetTarget(originalTarget.Get());
            resources.d2dContext->SetTransform(originalTransform);
            resources.surfaceWidthDip = originalWidth;
            resources.surfaceHeightDip = originalHeight;
            totalDocumentHeight = originalTotalHeight;
            scrollState.Restore(originalScroll, MaximumScrollOffset());
            printMode = false;
            preparedDocument = std::move(originalPrepared);
            interactionMap = std::move(originalInteraction);
            nonInteractiveRegions = std::move(originalNonInteractive);
        };
        auto finishStep = [&]() noexcept
        {
            restore();
            exporting = false;
            if (deferredInvalidate.exchange(false)) Invalidate();
        };

        try
        {
            auto recordPage = [&](float sourceTop, float clipHeight)
            {
                EditorPdfPage page;
                page.size = D2D1::SizeF(PdfPageWidth, PdfPageHeight);
                winrt::check_hresult(resources.d2dContext->CreateCommandList(page.commands.GetAddressOf()));
                resources.d2dContext->SetTarget(page.commands.Get());
                resources.d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
                resources.d2dContext->BeginDraw();
                resources.d2dContext->Clear(styleSheet.canvasColor);
                resources.d2dContext->SetTransform(
                    D2D1::Matrix3x2F::Translation(PdfPageMargin, PdfPageMargin));
                resources.d2dContext->PushAxisAlignedClip(
                    D2D1::RectF(
                        0.0f,
                        0.0f,
                        PdfContentWidth,
                        (std::max)(1.0f, clipHeight)),
                    D2D1_ANTIALIAS_MODE_ALIASED);
                scrollState.Set(sourceTop, MaximumScrollOffset());
                DrawDocument(frame);
                resources.d2dContext->PopAxisAlignedClip();
                auto result = resources.d2dContext->EndDraw();
                resources.d2dContext->SetTarget(nullptr);
                winrt::check_hresult(result);
                winrt::check_hresult(page.commands->Close());
                return page;
            };

            // The first recording for a page is a disposable preparation pass.
            // DrawDocument refines only this page's estimated geometry and
            // queues only this page's embedded resources.
            auto preparation = recordPage(state->nextSourceTop, PdfContentHeight);
            (void)preparation;

            if (!preparedDocument || !preparedDocument->geometry.Initialized())
            {
                finishStep();
                return {
                    EditorPdfExportResult::WaitingForAssets,
                    state->completedPages,
                    state->estimatedTotalPages,
                };
            }

            if (state->extents.size() != preparedDocument->geometry.Size())
            {
                state->extents.resize(preparedDocument->geometry.Size());
                for (std::size_t index = 0; index < preparedDocument->geometry.Size(); ++index)
                {
                    auto placement = preparedDocument->geometry.At(index);
                    state->extents[index] = {placement.top, placement.bottom};
                }
            }

            auto updateEstimatedTotal = [&]
            {
                auto remainingHeight = (std::max)(
                    0.0f,
                    preparedDocument->totalHeight - state->nextSourceTop);
                auto remainingPages = (std::max)(
                    std::size_t{1},
                    static_cast<std::size_t>(std::ceil(remainingHeight / PdfContentHeight)));
                state->estimatedTotalPages = state->completedPages + remainingPages;
            };
            updateEstimatedTotal();

            auto imageStillLoading = [](EditorPreparedDocument::Block const& block)
            {
                if (std::ranges::any_of(
                        block.images,
                        [](auto const& image) { return !image.Loaded(); }))
                    return true;
                return block.table && std::ranges::any_of(
                    block.table->imageDraws,
                    [](auto const& images)
                    {
                        return std::ranges::any_of(
                            images,
                            [](auto const& image) { return !image.Loaded(); });
                    });
            };

            auto pageLimit = state->nextSourceTop + PdfContentHeight;
            auto windowBegin = preparedDocument->geometry.FirstIntersecting(state->nextSourceTop);
            auto windowEnd = windowBegin;
            auto ready = true;
            auto imagesPending = renderCache.HasPendingImages();
            std::vector<std::size_t> pendingBlocks;
            while (windowEnd < preparedDocument->geometry.Size())
            {
                auto placement = preparedDocument->geometry.At(windowEnd);
                if (placement.top > pageLimit) break;
                state->extents[windowEnd] = {placement.top, placement.bottom};
                auto const& block = preparedDocument->blocks[windowEnd];
                if (!block.valid)
                {
                    ready = false;
                }
                else if (block.pendingMath
                    || block.pendingImage
                    || (imagesPending && block.containsImage && imageStillLoading(block)))
                {
                    pendingBlocks.push_back(windowEnd);
                }
                ++windowEnd;
            }
            // Keep the first block after the page boundary's top coordinate in
            // sync. It becomes the next page origin when the current page ends
            // at the previous complete block.
            if (windowEnd < preparedDocument->geometry.Size())
            {
                auto placement = preparedDocument->geometry.At(windowEnd);
                state->extents[windowEnd] = {placement.top, placement.bottom};
            }

            if (!pendingBlocks.empty())
            {
                // Poll asynchronous resources by rematerializing only the
                // affected page blocks. Do not release GIF sources here: that
                // would cancel the decode we are waiting for.
                for (auto index : pendingBlocks)
                {
                    preparedDocument->blocks[index].ReleaseVisualContent();
                    preparedDocument->embeddedBlocks.erase(index);
                    preparedDocument->layoutBlocks.erase(index);
                }
            }
            if (!ready || !pendingBlocks.empty())
            {
                finishStep();
                return {
                    EditorPdfExportResult::WaitingForAssets,
                    state->completedPages,
                    state->estimatedTotalPages,
                };
            }

            auto remaining = std::span<folia::PrintBlockExtent const>{state->extents}
                .subspan((std::min)(state->nextBlock, state->extents.size()));
            auto pageStep = folia::plan_next_print_page(
                remaining,
                state->nextSourceTop,
                preparedDocument->totalHeight,
                PdfContentHeight);
            auto const& slice = pageStep.slice;
            auto page = recordPage(
                slice.source_top,
                (std::min)(PdfContentHeight, slice.height()));
            resources.d2dContext->SetTarget(originalTarget.Get());
            resources.d2dContext->SetTransform(originalTransform);
            if (!state->printJob)
            {
                state->printJob = EditorPdfPrintJob::Begin(
                    path,
                    title,
                    resources.d2dDevice.Get(),
                    resources.wicFactory.Get());
            }
            state->printJob->AddPage(page);
            ++state->completedPages;
            state->nextBlock += pageStep.consumed_blocks;
            state->nextSourceTop = pageStep.next_source_top;
            if (pageStep.has_more)
            {
                updateEstimatedTotal();
                finishStep();
                return {
                    EditorPdfExportResult::InProgress,
                    state->completedPages,
                    state->estimatedTotalPages,
                };
            }

            state->printJob->Complete();
            state->completed = true;
            auto completed = state->completedPages;
            finishStep();
            pdfExportState.reset();
            renderCache.ClearTextLayouts();
            renderCache.ClearSvgDocuments();
            ClearPreparedDocument();
            Invalidate();
            return {EditorPdfExportResult::Completed, completed, completed};
        }
        catch (...)
        {
            finishStep();
            pdfExportState.reset();
            renderCache.ClearTextLayouts();
            renderCache.ClearSvgDocuments();
            ClearPreparedDocument();
            Invalidate();
            throw;
        }
    }

    void EditorSurfaceRenderer::CancelPdfExport()
    {
        if (!pdfExportState) return;
        pdfExportState.reset();
        renderCache.ClearTextLayouts();
        renderCache.ClearSvgDocuments();
        ClearPreparedDocument();
        Invalidate();
    }
}

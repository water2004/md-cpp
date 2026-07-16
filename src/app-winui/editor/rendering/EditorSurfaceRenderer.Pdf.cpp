#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "export/EditorPdfPrintJob.h"

import elmd.core.layout_plan;

namespace winrt::ElMd
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
        std::unique_ptr<PreparedDocument> prepared;
        EditorInteractionMap interaction;
        std::vector<D2D1_RECT_F> nonInteractive;
        float totalHeight = 0.0f;
        std::vector<elmd::PrintPageSlice> slices;
        std::size_t nextPage = 0;
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

    EditorSurfaceRenderer::PdfExportProgress EditorSurfaceRenderer::ExportPdfStep(
        std::filesystem::path const& path,
        std::wstring const& title,
        detail::EditorRenderFrame const& frame)
    {
        if (!resources.Ready()) winrt::throw_hresult(E_UNEXPECTED);
        if (rendering || resizing || exporting)
        {
            return {
                PdfExportResult::WaitingForAssets,
                pdfExportState ? pdfExportState->nextPage : 0,
                pdfExportState ? pdfExportState->slices.size() : 0,
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
        auto originalScroll = scrollOffset;
        auto originalScrollTarget = scrollTarget;
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
            scrollOffset = originalScroll;
            scrollTarget = originalScrollTarget;
            totalDocumentHeight = originalTotalHeight;
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
                scrollOffset = sourceTop;
                scrollTarget = sourceTop;
                DrawDocument(frame);
                resources.d2dContext->PopAxisAlignedClip();
                auto result = resources.d2dContext->EndDraw();
                resources.d2dContext->SetTarget(nullptr);
                winrt::check_hresult(result);
                winrt::check_hresult(page.commands->Close());
                return page;
            };

            if (state->slices.empty())
            {
                // The discarded preflight establishes print-width geometry and
                // starts asynchronous image/math preparation. Page count is
                // exact once these assets have settled.
                auto preflight = recordPage(0.0f, PdfContentHeight);
                (void)preflight;
                auto pendingMath = preparedDocument && std::any_of(
                    preparedDocument->blocks.begin(),
                    preparedDocument->blocks.end(),
                    [](PreparedDocument::Block const& block) { return block.pendingMath; });
                if (pendingMath || renderCache.HasPendingImages())
                {
                    finishStep();
                    return {PdfExportResult::WaitingForAssets, 0, 0};
                }

                std::vector<elmd::PrintBlockExtent> extents;
                if (preparedDocument)
                {
                    extents.reserve(preparedDocument->geometry.Size());
                    for (std::size_t index = 0; index < preparedDocument->geometry.Size(); ++index)
                    {
                        auto placement = preparedDocument->geometry.At(index);
                        extents.push_back({placement.top, placement.bottom});
                    }
                }
                auto documentBottom = preparedDocument
                    ? preparedDocument->totalHeight
                    : PdfContentHeight;
                state->slices = elmd::plan_print_pages(
                    extents,
                    0.0f,
                    documentBottom,
                    PdfContentHeight);
                if (state->slices.empty())
                    state->slices.push_back({0.0f, PdfContentHeight});
                state->printJob = EditorPdfPrintJob::Begin(
                    path,
                    title,
                    resources.d2dDevice.Get(),
                    resources.wicFactory.Get());
                auto total = state->slices.size();
                finishStep();
                return {PdfExportResult::InProgress, 0, total};
            }

            auto const& slice = state->slices[state->nextPage];
            auto page = recordPage(
                slice.source_top,
                (std::min)(PdfContentHeight, slice.height()));
            resources.d2dContext->SetTarget(originalTarget.Get());
            resources.d2dContext->SetTransform(originalTransform);
            state->printJob->AddPage(page);
            ++state->nextPage;
            auto completed = state->nextPage;
            auto total = state->slices.size();
            if (completed < total)
            {
                finishStep();
                return {PdfExportResult::InProgress, completed, total};
            }

            state->printJob->Complete();
            state->completed = true;
            finishStep();
            pdfExportState.reset();
            renderCache.ClearTextLayouts();
            renderCache.ClearSvgDocuments();
            ClearPreparedDocument();
            Invalidate();
            return {PdfExportResult::Completed, completed, total};
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

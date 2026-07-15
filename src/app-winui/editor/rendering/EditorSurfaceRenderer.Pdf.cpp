#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "export/EditorPdfPrintJob.h"

import elmd.core.layout_plan;

namespace winrt::ElMd
{
    EditorSurfaceRenderer::PdfExportResult EditorSurfaceRenderer::ExportPdf(
        std::filesystem::path const& path,
        std::wstring const& title,
        detail::EditorRenderFrame const& frame)
    {
        if (!resources.Ready()) winrt::throw_hresult(E_UNEXPECTED);
        if (rendering || resizing || exporting) return PdfExportResult::WaitingForAssets;
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
        auto originalTheme = theme;
        auto originalStyle = styleSheet;
        auto originalPrepared = std::move(preparedDocument);
        auto originalOwnerY = std::move(documentOwnerY);
        auto originalInteraction = std::move(interactionMap);
        auto originalNonInteractive = std::move(nonInteractiveRegions);
        bool restored = false;

        auto restore = [&]() noexcept
        {
            if (restored) return;
            restored = true;
            resources.d2dContext->SetTarget(originalTarget.Get());
            resources.d2dContext->SetTransform(originalTransform);
            resources.surfaceWidthDip = originalWidth;
            resources.surfaceHeightDip = originalHeight;
            scrollOffset = originalScroll;
            scrollTarget = originalScrollTarget;
            totalDocumentHeight = originalTotalHeight;
            theme = originalTheme;
            styleSheet = originalStyle;
            printMode = false;
            preparedDocument = std::move(originalPrepared);
            documentOwnerY = std::move(originalOwnerY);
            interactionMap = std::move(originalInteraction);
            nonInteractiveRegions = std::move(originalNonInteractive);
            try
            {
                renderCache.ClearTextLayouts();
                renderCache.ClearSvgDocuments();
                resources.RebuildTextFormats(styleSheet);
                resources.ResetBrushes();
                resources.EnsureFrameResources(styleSheet);
            }
            catch (...) {}
        };
        auto finish = [&]()
        {
            restore();
            exporting = false;
            if (deferredInvalidate.exchange(false)) Invalidate();
        };

        try
        {
            constexpr float pageWidth = 210.0f / 25.4f * 96.0f;
            constexpr float pageHeight = 297.0f / 25.4f * 96.0f;
            constexpr float pageMargin = 48.0f;
            constexpr float contentWidth = pageWidth - pageMargin * 2.0f;
            constexpr float contentHeight = pageHeight - pageMargin * 2.0f;

            printMode = true;
            theme = Theme::Light;
            styleSheet = CreateEditorStyleSheet(false);
            preparedDocument.reset();
            documentOwnerY.clear();
            interactionMap = {};
            nonInteractiveRegions.clear();
            resources.surfaceWidthDip = contentWidth;
            resources.surfaceHeightDip = contentHeight;
            scrollOffset = 0.0f;
            scrollTarget = 0.0f;
            renderCache.ClearTextLayouts();
            renderCache.ClearSvgDocuments();
            resources.RebuildTextFormats(styleSheet);
            resources.ResetBrushes();
            resources.EnsureFrameResources(styleSheet);

            auto recordPage = [&](float sourceTop, float clipHeight)
            {
                EditorPdfPage page;
                page.size = D2D1::SizeF(pageWidth, pageHeight);
                winrt::check_hresult(resources.d2dContext->CreateCommandList(page.commands.GetAddressOf()));
                resources.d2dContext->SetTarget(page.commands.Get());
                resources.d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
                resources.d2dContext->BeginDraw();
                resources.d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::White));
                resources.d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(pageMargin, pageMargin));
                resources.d2dContext->PushAxisAlignedClip(
                    D2D1::RectF(0.0f, 0.0f, contentWidth, (std::max)(1.0f, clipHeight)),
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

            // The first pass requests every embedded resource and establishes
            // final block extents at print width. It is intentionally discarded
            // because the block-aware page boundaries are not known yet.
            auto preflight = recordPage(0.0f, contentHeight);
            (void)preflight;
            auto pendingMath = preparedDocument && std::any_of(
                preparedDocument->blocks.begin(),
                preparedDocument->blocks.end(),
                [](PreparedDocument::Block const& block) { return block.pendingMath; });
            if (pendingMath || renderCache.HasPendingImages())
            {
                finish();
                return PdfExportResult::WaitingForAssets;
            }

            std::vector<elmd::PrintBlockExtent> extents;
            if (preparedDocument)
            {
                extents.reserve(preparedDocument->placements.size());
                for (auto const& placement : preparedDocument->placements)
                    extents.push_back({placement.top, placement.bottom});
            }
            auto documentBottom = preparedDocument ? preparedDocument->totalHeight : contentHeight;
            auto slices = elmd::plan_print_pages(extents, 0.0f, documentBottom, contentHeight);
            std::vector<EditorPdfPage> pages;
            pages.reserve(slices.size());
            for (auto const& slice : slices)
                pages.push_back(recordPage(slice.source_top, (std::min)(contentHeight, slice.height())));

            resources.d2dContext->SetTarget(originalTarget.Get());
            resources.d2dContext->SetTransform(originalTransform);
            EditorPdfPrintJob::Write(path, title, resources.d2dDevice.Get(), resources.wicFactory.Get(), pages);
            finish();
            return PdfExportResult::Completed;
        }
        catch (...)
        {
            finish();
            throw;
        }
    }

}

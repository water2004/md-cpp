#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"

namespace winrt::ElMd
{
    EditorSurfaceRenderer::EditorSurfaceRenderer() = default;

    EditorSurfaceRenderer::~EditorSurfaceRenderer()
    {
        renderCache.Detach();
        mathJax.SetCompletionCallback({});
        svgNormalizer.SetCompletionCallback({});
        mermaid.SetCompletionCallback({});
        std::scoped_lock lock(invalidationState->mutex);
        invalidationState->active = false;
        invalidationState->callback = {};
    }

    void EditorSurfaceRenderer::SetTheme(elmd::ThemeProfile const& value)
    {
        themeProfile = value;
        ++themeRevision;
        styleSheet = CreateEditorStyleSheet(themeProfile);
        renderCache.ClearTextLayouts();
        renderCache.ClearSvgDocuments();
        resources.RebuildTextFormats(styleSheet);
        resources.ResetBrushes();
        ClearPreparedDocument();
    }

    void EditorSurfaceRenderer::SetMathRenderingEnabled(bool enabled)
    {
        if (mathJax.Enabled() == enabled) return;
        mathJax.SetEnabled(enabled);
        ++embeddedGeneration;
        renderCache.ClearSvgDocuments();
        ClearPreparedDocument();
        Invalidate();
    }

    bool EditorSurfaceRenderer::MathRenderingEnabled() const
    {
        return mathJax.Enabled();
    }

    void EditorSurfaceRenderer::ResetDocumentCaches()
    {
        mathJax.Clear();
        treeSitter.Clear();
        renderCache.ClearTextLayouts();
        renderCache.ClearSvgDocuments();
        ClearPreparedDocument();
    }

    void EditorSurfaceRenderer::ClearPreparedDocument()
    {
        preparedDocument.reset();
        documentOwnerY.clear();
    }

    void EditorSurfaceRenderer::SetInvalidateCallback(std::function<void()> callback)
    {
        std::scoped_lock lock(invalidationState->mutex);
        invalidationState->callback = std::move(callback);
    }

    void EditorSurfaceRenderer::Invalidate()
    {
        if (rendering || exporting) { deferredInvalidate = true; return; }
        std::function<void()> callback;
        {
            std::scoped_lock lock(invalidationState->mutex);
            if (invalidationState->active) callback = invalidationState->callback;
        }
        if (callback) callback();
    }

    void EditorSurfaceRenderer::Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel)
    {
        if (resources.Ready()) return;
        resources.Initialize(panel, styleSheet);
        auto dispatcher = panel.DispatcherQueue();
        renderCache.Attach(dispatcher, [this] { Invalidate(); });
        auto completion = [this, dispatcher]
        {
            if (mathInvalidationQueued.exchange(true)) return;
            if (!dispatcher.TryEnqueue([this]
                {
                    mathInvalidationQueued = false;
                    ++embeddedGeneration;
                    Invalidate();
                })) mathInvalidationQueued = false;
        };
        mathJax.SetCompletionCallback(completion);
        svgNormalizer.SetCompletionCallback(completion);
        mermaid.SetCompletionCallback(std::move(completion));
    }

    void EditorSurfaceRenderer::Resize(
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel,
        double width,
        double height)
    {
        if (resizing) return;
        resizing = true;
        struct ResetFlag { bool& value; ~ResetFlag() { value = false; } } reset{resizing};
        auto result = resources.Resize(panel, width, height);
        if (!result.resized) return;
        if (result.widthChanged)
        {
            renderCache.ClearTextLayouts();
            ClearPreparedDocument();
        }
        scrollOffset = (std::min)(scrollOffset, MaximumScrollOffset());
        scrollTarget = (std::min)(scrollTarget, MaximumScrollOffset());
    }

    void EditorSurfaceRenderer::Render(detail::EditorRenderFrame const& frame)
    {
        if (!resources.Ready()) return;
        if (resizing || rendering)
        {
            // A source edit may request a frame while an asynchronous math or
            // SVG invalidation is painting. Never drop that newer frame: the
            // deferred callback obtains the latest session model after this
            // render completes.
            deferredInvalidate = true;
            return;
        }
        rendering = true;
        struct Reset { EditorSurfaceRenderer& owner; ~Reset() { owner.rendering = false; if (owner.deferredInvalidate.exchange(false)) owner.Invalidate(); } } reset{*this};
        resources.EnsureFrameResources(styleSheet);
        resources.d2dContext->BeginDraw();
        resources.d2dContext->Clear(styleSheet.canvasColor);
        DrawDocument(frame);
        auto ended = resources.d2dContext->EndDraw();
        if (ended == D2DERR_RECREATE_TARGET) { resources.ResetTargets(); return; }
        if (FAILED(ended)) return;
        auto presented = resources.swapChain->Present(0, 0);
        if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET) resources.ResetTargets();
    }
}

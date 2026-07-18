#include "pch.h"
#include "editor/rendering/EditorRenderCache.h"
#include "media/EditorGifDecoder.h"

namespace winrt::Folia
{
    EditorRenderCache::~EditorRenderCache()
    {
        Detach();
    }

    void EditorRenderCache::Attach(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher, std::function<void()> invalidate)
    {
        {
            std::scoped_lock lock(remoteState->mutex);
            remoteState->dispatcher = dispatcher;
            remoteState->invalidate = std::move(invalidate);
            remoteState->active = true;
        }
    }

    void EditorRenderCache::Detach()
    {
        StopAnimationPump();
        {
            std::scoped_lock lock(remoteState->mutex);
            remoteState->active = false;
            remoteState->invalidate = {};
            remoteState->dispatcher = nullptr;
            remoteState->data.clear();
            remoteState->dimensions.clear();
            remoteState->pending.clear();
            remoteState->failed.clear();
            remoteState->dimensionPending.clear();
            remoteState->dimensionFailed.clear();
            remoteState->order.clear();
            remoteState->dimensionOrder.clear();
            remoteState->bytes = 0;
            ++remoteState->generation;
        }
        ClearDeviceResources();
    }

    void EditorRenderCache::ClearTextLayouts()
    {
        textLayouts.clear();
        textLayoutOrder.clear();
        textLayoutBytes = 0;
    }

    void EditorRenderCache::ClearSvgDocuments()
    {
        svgDocuments.clear();
        svgDocumentOrder.clear();
        svgDocumentBytes = 0;
    }

    void EditorRenderCache::ClearDeviceResources()
    {
        ClearTextLayouts();
        ClearSvgDocuments();
        rasterImages.clear();
        for (auto const& [key, pending] : pendingGifImages) pending.decode->Cancel();
        pendingGifImages.clear();
        rasterImageFailures.clear();
        rasterImageOrder.clear();
        rasterImageBytes = 0;
    }

    std::uint64_t EditorRenderCache::RemoteImageGeneration() const
    {
        return remoteState->generation.load();
    }

    bool EditorRenderCache::HasPendingImages() const
    {
        if (!pendingGifImages.empty()) return true;
        std::scoped_lock lock(remoteState->mutex);
        // Metadata probes refine layout but are never required to paint an
        // image. PDF export must wait only for actual image content/decoding;
        // otherwise an unrelated off-page range request can stall a page.
        return !remoteState->pending.empty();
    }
}

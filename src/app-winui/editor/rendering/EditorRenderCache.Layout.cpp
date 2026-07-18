#include "pch.h"
#include "editor/rendering/EditorRenderCache.h"

namespace winrt::Folia
{
    ::Microsoft::WRL::ComPtr<IDWriteTextLayout> EditorRenderCache::FindTextLayout(std::uint64_t key)
    {
        auto found = textLayouts.find(key);
        if (found == textLayouts.end()) return {};
        if (auto order = std::find(textLayoutOrder.begin(), textLayoutOrder.end(), key); order != textLayoutOrder.end())
        {
            textLayoutOrder.erase(order);
            textLayoutOrder.push_back(key);
        }
        return found->second.layout;
    }

    void EditorRenderCache::StoreTextLayout(std::uint64_t key, ::Microsoft::WRL::ComPtr<IDWriteTextLayout> const& layout, std::size_t bytes)
    {
        if (!layout) return;
        constexpr std::size_t budget = 16 * 1024 * 1024;
        constexpr std::size_t limit = 512;
        while (!textLayoutOrder.empty() && (textLayoutBytes + bytes > budget || textLayouts.size() >= limit))
        {
            auto oldest = textLayoutOrder.front();
            textLayoutOrder.pop_front();
            auto found = textLayouts.find(oldest);
            if (found == textLayouts.end()) continue;
            textLayoutBytes -= found->second.bytes;
            textLayouts.erase(found);
        }
        if (bytes > budget) return;
        textLayoutBytes += bytes;
        textLayoutOrder.push_back(key);
        textLayouts.emplace(key, CachedTextLayout{ layout, bytes });
    }

    ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> EditorRenderCache::FindSvgDocument(
        std::uint64_t renderId)
    {
        return svgDocuments.Find(renderId);
    }

    bool EditorRenderCache::QueueSvgDocument(
        std::uint64_t renderId,
        std::string const& source,
        float width,
        float height,
        bool highPriority)
    {
        return svgDocuments.Queue(renderId, source, width, height, highPriority);
    }

    ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> EditorRenderCache::FindOrCreateSvgDocument(ID2D1DeviceContext5* context, std::uint64_t renderId, std::string const& source, float width, float height)
    {
        return svgDocuments.FindOrCreate(context, renderId, source, width, height);
    }
}

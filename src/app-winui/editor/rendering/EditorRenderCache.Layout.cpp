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
        if (auto found = svgDocuments.find(renderId); found != svgDocuments.end())
        {
            svgDocumentOrder.splice(svgDocumentOrder.end(), svgDocumentOrder, found->second.order);
            return found->second.document;
        }
        return {};
    }

    ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> EditorRenderCache::FindOrCreateSvgDocument(ID2D1DeviceContext5* context, std::uint64_t renderId, std::string const& source, float width, float height)
    {
        if (!context || renderId == 0 || source.empty() || width <= 0.0f || height <= 0.0f) return {};
        if (auto document = FindSvgDocument(renderId)) return document;
        auto allocation = GlobalAlloc(GMEM_MOVEABLE, source.size());
        if (!allocation) return {};
        auto bytes = static_cast<char*>(GlobalLock(allocation));
        if (!bytes)
        {
            GlobalFree(allocation);
            return {};
        }
        std::memcpy(bytes, source.data(), source.size());
        GlobalUnlock(allocation);
        ::Microsoft::WRL::ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(allocation, TRUE, stream.GetAddressOf())) || !stream)
        {
            GlobalFree(allocation);
            return {};
        }
        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
        if (FAILED(context->CreateSvgDocument(stream.Get(), D2D1::SizeF(width, height), document.GetAddressOf())) || !document) return {};
        constexpr std::size_t budget = 24 * 1024 * 1024;
        constexpr std::size_t limit = 512;
        auto resourceCost = (std::max)(std::size_t{16 * 1024}, source.size() * 8);
        while (!svgDocumentOrder.empty() && (svgDocumentBytes + resourceCost > budget || svgDocuments.size() >= limit))
        {
            auto oldest = std::move(svgDocumentOrder.front());
            svgDocumentOrder.pop_front();
            auto found = svgDocuments.find(oldest);
            if (found == svgDocuments.end()) continue;
            svgDocumentBytes -= found->second.bytes;
            svgDocuments.erase(found);
        }
        if (resourceCost <= budget)
        {
            svgDocumentBytes += resourceCost;
            svgDocumentOrder.push_back(renderId);
            svgDocuments.emplace(renderId, CachedSvgDocument{
                document,
                resourceCost,
                std::prev(svgDocumentOrder.end()),
            });
        }
        return document;
    }
}

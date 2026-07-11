#pragma once

#include "EditorRenderResources.h"

namespace winrt::ElMd
{
    struct EditorRenderCache
    {
        struct RasterImage
        {
            ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            float width = 0.0f;
            float height = 0.0f;
            std::size_t bytes = 0;
        };

        ~EditorRenderCache();

        void Attach(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher, std::function<void()> invalidate);
        void Detach();
        void ClearTextLayouts();
        void ClearSvgDocuments();
        void ClearDeviceResources();
        std::uint64_t RemoteImageGeneration() const;
        std::optional<RasterImage> LoadRasterImage(EditorRenderResources const& resources, std::wstring const& baseDirectory, std::string_view source);
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> FindTextLayout(std::uint64_t key);
        void StoreTextLayout(std::uint64_t key, ::Microsoft::WRL::ComPtr<IDWriteTextLayout> const& layout, std::size_t bytes);
        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> FindOrCreateSvgDocument(ID2D1DeviceContext5* context, std::uint64_t renderId, std::string const& source, float width, float height);

    private:
        struct CachedSvgDocument
        {
            ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
            std::size_t bytes = 0;
        };

        struct CachedTextLayout
        {
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            std::size_t bytes = 0;
        };

        struct RemoteState
        {
            std::mutex mutex;
            std::function<void()> invalidate;
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
            std::unordered_map<std::string, std::vector<std::uint8_t>> data;
            std::unordered_set<std::string> pending;
            std::unordered_set<std::string> failed;
            std::deque<std::string> order;
            std::size_t bytes = 0;
            std::atomic_uint64_t generation = 0;
            bool active = false;
        };

        void QueueRemoteImage(std::string source);

        std::unordered_map<std::uint64_t, CachedTextLayout> textLayouts;
        std::deque<std::uint64_t> textLayoutOrder;
        std::size_t textLayoutBytes = 0;
        std::unordered_map<std::wstring, RasterImage> rasterImages;
        std::unordered_set<std::wstring> rasterImageFailures;
        std::deque<std::wstring> rasterImageOrder;
        std::size_t rasterImageBytes = 0;
        std::unordered_map<std::uint64_t, CachedSvgDocument> svgDocuments;
        std::deque<std::uint64_t> svgDocumentOrder;
        std::size_t svgDocumentBytes = 0;
        std::shared_ptr<RemoteState> remoteState = std::make_shared<RemoteState>();
    };
}

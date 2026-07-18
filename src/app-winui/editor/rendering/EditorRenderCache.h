#pragma once

#include <list>

#include "editor/rendering/EditorRenderResources.h"

namespace winrt::Folia
{
    struct DecodedGifAnimation;
    struct GifInitialDecode;

    struct EditorRenderCache
    {
        struct RasterImage
        {
            ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            std::shared_ptr<DecodedGifAnimation> animation;
            float width = 0.0f;
            float height = 0.0f;
            std::size_t bytes = 0;
        };

        struct ImageDimensions
        {
            float width = 0.0f;
            float height = 0.0f;
        };

        struct SvgSource
        {
            std::optional<std::string> source;
            bool candidate = false;
            bool pending = false;
        };

        ~EditorRenderCache();

        void Attach(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher, std::function<void()> invalidate);
        void Detach();
        void ClearTextLayouts();
        void ClearSvgDocuments();
        void ClearDeviceResources();
        bool HasPendingImages() const;
        std::uint64_t RemoteImageGeneration() const;
        std::optional<ImageDimensions> ProbeImageDimensions(EditorRenderResources const& resources, std::wstring const& baseDirectory, std::string_view source);
        SvgSource LoadSvgSource(std::wstring const& baseDirectory, std::string_view source, bool loadContent = true);
        std::optional<RasterImage> LoadRasterImage(EditorRenderResources const& resources, std::wstring const& baseDirectory, std::string_view source);
        void ReleaseGifImage(std::wstring const& baseDirectory, std::string_view source);
        ID2D1Bitmap1* CurrentBitmap(RasterImage const& image, std::chrono::milliseconds& untilNextFrame) const;
        void RequestAnimationFrame(std::chrono::milliseconds delay);
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> FindTextLayout(std::uint64_t key);
        void StoreTextLayout(std::uint64_t key, ::Microsoft::WRL::ComPtr<IDWriteTextLayout> const& layout, std::size_t bytes);
        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> FindSvgDocument(std::uint64_t renderId);
        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> FindOrCreateSvgDocument(ID2D1DeviceContext5* context, std::uint64_t renderId, std::string const& source, float width, float height);

    private:
        struct CachedSvgDocument
        {
            ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
            std::size_t bytes = 0;
            std::list<std::uint64_t>::iterator order;
        };

        struct CachedTextLayout
        {
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            std::size_t bytes = 0;
        };

        struct PendingGifImage
        {
            std::shared_ptr<GifInitialDecode> decode;
            std::shared_ptr<std::vector<std::uint8_t>> encodedBacking;
            std::filesystem::path path;
        };

        struct RemoteState
        {
            std::mutex mutex;
            std::function<void()> invalidate;
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
            std::unordered_map<std::string, std::vector<std::uint8_t>> data;
            std::unordered_map<std::string, ImageDimensions> dimensions;
            std::unordered_set<std::string> pending;
            std::unordered_set<std::string> failed;
            std::unordered_set<std::string> dimensionPending;
            std::unordered_set<std::string> dimensionFailed;
            std::deque<std::string> order;
            std::deque<std::string> dimensionOrder;
            std::size_t bytes = 0;
            std::atomic_uint64_t generation = 0;
            bool active = false;
        };

        void QueueRemoteImage(std::string source);
        void QueueRemoteImageDimensions(std::string source);
        void StopAnimationPump();

        std::unordered_map<std::uint64_t, CachedTextLayout> textLayouts;
        std::deque<std::uint64_t> textLayoutOrder;
        std::size_t textLayoutBytes = 0;
        std::unordered_map<std::wstring, RasterImage> rasterImages;
        std::unordered_map<std::wstring, ImageDimensions> imageDimensions;
        std::deque<std::wstring> imageDimensionOrder;
        std::unordered_set<std::wstring> imageDimensionMisses;
        std::unordered_map<std::wstring, PendingGifImage> pendingGifImages;
        std::unordered_set<std::wstring> rasterImageFailures;
        std::deque<std::wstring> rasterImageOrder;
        std::size_t rasterImageBytes = 0;
        std::unordered_map<std::uint64_t, CachedSvgDocument> svgDocuments;
        std::list<std::uint64_t> svgDocumentOrder;
        std::size_t svgDocumentBytes = 0;
        std::shared_ptr<RemoteState> remoteState = std::make_shared<RemoteState>();
        winrt::event_token animationRenderingToken{};
        std::optional<std::chrono::steady_clock::time_point> animationDeadline;
        bool animationPumpActive = false;
    };
}

#pragma once

namespace winrt::Folia
{
    struct GifInitialFrame
    {
        UINT frameCount = 0;
        UINT width = 0;
        UINT height = 0;
        UINT firstLeft = 0;
        UINT firstTop = 0;
        UINT firstWidth = 0;
        UINT firstHeight = 0;
        UINT firstDisposal = 0;
        bool firstTransparent = false;
        UINT firstTransparentIndex = 0;
        std::chrono::milliseconds firstDuration{100};
        std::array<std::uint8_t, 4> background{};
        std::vector<std::uint8_t> canvas;
    };

    struct GifInitialDecode
    {
        bool Complete() const;
        bool Failed() const;
        void Cancel();
        std::shared_ptr<GifInitialFrame const> Result() const;

    private:
        mutable std::mutex mutex;
        std::shared_ptr<GifInitialFrame const> result;
        bool complete = false;
        bool failed = false;
        bool cancelled = false;

        friend std::shared_ptr<GifInitialDecode> QueueGifInitialDecode(
            std::filesystem::path,
            std::shared_ptr<std::vector<std::uint8_t>>,
            std::function<void()>,
            std::size_t);
    };

    std::shared_ptr<GifInitialDecode> QueueGifInitialDecode(
        std::filesystem::path path,
        std::shared_ptr<std::vector<std::uint8_t>> encodedBacking,
        std::function<void()> completion,
        std::size_t runtimeByteBudget);

    struct DecodedGifAnimation
    {
        ~DecodedGifAnimation();
        ID2D1Bitmap1* CurrentBitmap(std::chrono::milliseconds& untilNextFrame);
        ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> const& Bitmap() const;
        UINT Width() const;
        UINT Height() const;
        std::size_t MemoryCost() const;

    private:
        struct State;
        explicit DecodedGifAnimation(std::shared_ptr<State> value);
        std::shared_ptr<State> state;

        friend std::shared_ptr<DecodedGifAnimation> DecodeGifAnimation(
            IWICImagingFactory*,
            ID2D1DeviceContext*,
            IWICBitmapDecoder*,
            std::shared_ptr<std::vector<std::uint8_t> const>,
            std::shared_ptr<GifInitialFrame const>,
            std::filesystem::path,
            std::function<void()>,
            std::size_t);
    };

    std::shared_ptr<DecodedGifAnimation> DecodeGifAnimation(
        IWICImagingFactory* factory,
        ID2D1DeviceContext* context,
        IWICBitmapDecoder* decoder,
        std::shared_ptr<std::vector<std::uint8_t> const> encodedBacking,
        std::shared_ptr<GifInitialFrame const> initialFrame,
        std::filesystem::path sourcePath,
        std::function<void()> frameCompletion,
        std::size_t runtimeByteBudget);
}

#pragma once

namespace winrt::ElMd
{
    struct DecodedGifAnimation
    {
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
            std::size_t);
    };

    std::shared_ptr<DecodedGifAnimation> DecodeGifAnimation(
        IWICImagingFactory* factory,
        ID2D1DeviceContext* context,
        IWICBitmapDecoder* decoder,
        std::shared_ptr<std::vector<std::uint8_t> const> encodedBacking,
        std::size_t runtimeByteBudget);
}

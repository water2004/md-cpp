#pragma once

namespace winrt::ElMd
{
    struct DecodedGifAnimation
    {
        struct Frame
        {
            ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            std::chrono::milliseconds duration{100};
        };

        std::vector<Frame> frames;
        std::chrono::milliseconds cycle{0};
        UINT width = 0;
        UINT height = 0;
        std::size_t bytes = 0;
    };

    std::optional<DecodedGifAnimation> DecodeGifAnimation(
        IWICImagingFactory* factory,
        ID2D1DeviceContext* context,
        IWICBitmapDecoder* decoder,
        std::size_t decodedByteBudget);
}

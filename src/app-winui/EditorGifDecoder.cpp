#include "pch.h"
#include "EditorGifDecoder.h"

namespace
{
    struct GifFrameInfo
    {
        UINT left = 0;
        UINT top = 0;
        UINT width = 0;
        UINT height = 0;
        UINT disposal = 0;
        bool transparent = false;
        UINT transparentIndex = 0;
    };

    std::optional<UINT> MetadataUnsigned(IWICMetadataQueryReader* metadata, wchar_t const* name)
    {
        if (!metadata) return std::nullopt;
        PROPVARIANT value{};
        PropVariantInit(&value);
        auto result = std::optional<UINT>{};
        if (SUCCEEDED(metadata->GetMetadataByName(name, &value)))
        {
            switch (value.vt)
            {
            case VT_UI1: result = value.bVal; break;
            case VT_UI2: result = value.uiVal; break;
            case VT_UI4: result = value.ulVal; break;
            case VT_BOOL: result = value.boolVal == VARIANT_TRUE ? 1u : 0u; break;
            default: break;
            }
        }
        PropVariantClear(&value);
        return result;
    }

    GifFrameInfo ReadFrameInfo(IWICBitmapFrameDecode* frame)
    {
        GifFrameInfo info;
        if (!frame || FAILED(frame->GetSize(&info.width, &info.height))) return info;
        ::Microsoft::WRL::ComPtr<IWICMetadataQueryReader> metadata;
        if (FAILED(frame->GetMetadataQueryReader(metadata.GetAddressOf())) || !metadata) return info;
        info.left = MetadataUnsigned(metadata.Get(), L"/imgdesc/Left").value_or(0);
        info.top = MetadataUnsigned(metadata.Get(), L"/imgdesc/Top").value_or(0);
        info.width = MetadataUnsigned(metadata.Get(), L"/imgdesc/Width").value_or(info.width);
        info.height = MetadataUnsigned(metadata.Get(), L"/imgdesc/Height").value_or(info.height);
        info.disposal = MetadataUnsigned(metadata.Get(), L"/grctlext/Disposal").value_or(0);
        info.transparent = MetadataUnsigned(metadata.Get(), L"/grctlext/TransparencyFlag").value_or(0) != 0;
        info.transparentIndex = MetadataUnsigned(metadata.Get(), L"/grctlext/TransparentColorIndex").value_or(0);
        return info;
    }

    std::chrono::milliseconds FrameDelay(IWICBitmapFrameDecode* frame)
    {
        if (!frame) return std::chrono::milliseconds{100};
        ::Microsoft::WRL::ComPtr<IWICMetadataQueryReader> metadata;
        if (FAILED(frame->GetMetadataQueryReader(metadata.GetAddressOf())) || !metadata)
            return std::chrono::milliseconds{100};
        auto delay = MetadataUnsigned(metadata.Get(), L"/grctlext/Delay").value_or(0);
        auto milliseconds = delay == 0 ? 100u : delay * 10u;
        return std::chrono::milliseconds{(std::clamp)(milliseconds, 20u, 10000u)};
    }

    std::array<std::uint8_t, 4> BackgroundPixel(
        IWICImagingFactory* factory,
        IWICBitmapDecoder* decoder,
        GifFrameInfo const& firstFrame)
    {
        std::array<std::uint8_t, 4> pixel{};
        ::Microsoft::WRL::ComPtr<IWICMetadataQueryReader> metadata;
        if (!factory || !decoder
            || FAILED(decoder->GetMetadataQueryReader(metadata.GetAddressOf()))
            || !metadata) return pixel;
        auto backgroundIndex = MetadataUnsigned(metadata.Get(), L"/logscrdesc/BackgroundColorIndex");
        if (!backgroundIndex || (firstFrame.transparent && firstFrame.transparentIndex == *backgroundIndex)) return pixel;
        ::Microsoft::WRL::ComPtr<IWICPalette> palette;
        if (FAILED(factory->CreatePalette(palette.GetAddressOf())) || !palette
            || FAILED(decoder->CopyPalette(palette.Get()))) return pixel;
        std::array<WICColor, 256> colors{};
        UINT count = 0;
        if (FAILED(palette->GetColors(static_cast<UINT>(colors.size()), colors.data(), &count))
            || *backgroundIndex >= count) return pixel;
        auto color = colors[*backgroundIndex];
        auto alpha = static_cast<std::uint8_t>(color >> 24);
        auto premultiply = [alpha](std::uint8_t channel)
        {
            return static_cast<std::uint8_t>((static_cast<UINT>(channel) * alpha + 127u) / 255u);
        };
        pixel[0] = premultiply(static_cast<std::uint8_t>(color));
        pixel[1] = premultiply(static_cast<std::uint8_t>(color >> 8));
        pixel[2] = premultiply(static_cast<std::uint8_t>(color >> 16));
        pixel[3] = alpha;
        return pixel;
    }

    void FillRect(
        std::vector<std::uint8_t>& canvas,
        UINT canvasWidth,
        UINT canvasHeight,
        GifFrameInfo const& rect,
        std::array<std::uint8_t, 4> const& pixel)
    {
        auto right = (std::min)(canvasWidth, rect.left + rect.width);
        auto bottom = (std::min)(canvasHeight, rect.top + rect.height);
        for (auto y = rect.top; y < bottom; ++y)
        {
            for (auto x = rect.left; x < right; ++x)
            {
                auto offset = (static_cast<std::size_t>(y) * canvasWidth + x) * 4;
                std::copy(pixel.begin(), pixel.end(), canvas.begin() + offset);
            }
        }
    }

    void CompositeFrame(
        std::vector<std::uint8_t>& canvas,
        UINT canvasWidth,
        UINT canvasHeight,
        GifFrameInfo const& frame,
        std::vector<std::uint8_t> const& pixels)
    {
        auto right = (std::min)(canvasWidth, frame.left + frame.width);
        auto bottom = (std::min)(canvasHeight, frame.top + frame.height);
        for (auto y = frame.top; y < bottom; ++y)
        {
            for (auto x = frame.left; x < right; ++x)
            {
                auto sourceOffset = (static_cast<std::size_t>(y - frame.top) * frame.width + (x - frame.left)) * 4;
                auto targetOffset = (static_cast<std::size_t>(y) * canvasWidth + x) * 4;
                auto alpha = pixels[sourceOffset + 3];
                if (alpha == 0) continue;
                if (alpha == 255)
                {
                    std::copy_n(pixels.begin() + sourceOffset, 4, canvas.begin() + targetOffset);
                    continue;
                }
                auto inverse = 255u - alpha;
                for (auto channel = 0u; channel < 3u; ++channel)
                {
                    canvas[targetOffset + channel] = static_cast<std::uint8_t>(
                        pixels[sourceOffset + channel]
                        + (static_cast<UINT>(canvas[targetOffset + channel]) * inverse + 127u) / 255u);
                }
                canvas[targetOffset + 3] = static_cast<std::uint8_t>(
                    alpha + (static_cast<UINT>(canvas[targetOffset + 3]) * inverse + 127u) / 255u);
            }
        }
    }
}

namespace winrt::ElMd
{
    std::optional<DecodedGifAnimation> DecodeGifAnimation(
        IWICImagingFactory* factory,
        ID2D1DeviceContext* context,
        IWICBitmapDecoder* decoder,
        std::size_t decodedByteBudget)
    {
        if (!factory || !context || !decoder) return std::nullopt;
        GUID containerFormat{};
        UINT frameCount = 0;
        if (FAILED(decoder->GetContainerFormat(&containerFormat))
            || !IsEqualGUID(containerFormat, GUID_ContainerFormatGif)
            || FAILED(decoder->GetFrameCount(&frameCount))
            || frameCount < 2
            || frameCount > 256) return std::nullopt;

        ::Microsoft::WRL::ComPtr<IWICMetadataQueryReader> decoderMetadata;
        decoder->GetMetadataQueryReader(decoderMetadata.GetAddressOf());
        auto canvasWidth = MetadataUnsigned(decoderMetadata.Get(), L"/logscrdesc/Width").value_or(0);
        auto canvasHeight = MetadataUnsigned(decoderMetadata.Get(), L"/logscrdesc/Height").value_or(0);
        ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> firstRawFrame;
        if (FAILED(decoder->GetFrame(0, firstRawFrame.GetAddressOf())) || !firstRawFrame) return std::nullopt;
        auto firstInfo = ReadFrameInfo(firstRawFrame.Get());
        if (canvasWidth == 0) canvasWidth = firstInfo.left + firstInfo.width;
        if (canvasHeight == 0) canvasHeight = firstInfo.top + firstInfo.height;
        auto canvasPixels = static_cast<std::uint64_t>(canvasWidth) * canvasHeight;
        auto canvasBytes = canvasPixels * 4ull;
        if (canvasWidth == 0 || canvasHeight == 0
            || canvasPixels > 16ull * 1024ull * 1024ull
            || canvasBytes * frameCount > decodedByteBudget) return std::nullopt;

        auto background = BackgroundPixel(factory, decoder, firstInfo);
        std::vector<std::uint8_t> canvas(static_cast<std::size_t>(canvasBytes));
        for (auto offset = std::size_t{0}; offset < canvas.size(); offset += 4)
            std::copy(background.begin(), background.end(), canvas.begin() + offset);
        std::vector<std::uint8_t> restoreCanvas;
        GifFrameInfo previousInfo;
        DecodedGifAnimation result;
        result.width = canvasWidth;
        result.height = canvasHeight;
        result.bytes = static_cast<std::size_t>(canvasBytes * frameCount);
        result.frames.reserve(frameCount);
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);

        for (UINT index = 0; index < frameCount; ++index)
        {
            ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(index, frame.GetAddressOf())) || !frame) return std::nullopt;
            auto info = ReadFrameInfo(frame.Get());
            if (info.width == 0 || info.height == 0
                || info.left >= canvasWidth || info.top >= canvasHeight
                || static_cast<std::uint64_t>(info.width) * info.height > 16ull * 1024ull * 1024ull)
                return std::nullopt;
            ::Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            auto stride = info.width * 4u;
            std::vector<std::uint8_t> framePixels(static_cast<std::size_t>(stride) * info.height);
            if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))
                || FAILED(converter->Initialize(
                    frame.Get(),
                    GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0.0,
                    WICBitmapPaletteTypeMedianCut))
                || FAILED(converter->CopyPixels(
                    nullptr,
                    stride,
                    static_cast<UINT>(framePixels.size()),
                    framePixels.data()))) return std::nullopt;

            if (index > 0)
            {
                if (previousInfo.disposal == 2) FillRect(canvas, canvasWidth, canvasHeight, previousInfo, background);
                else if (previousInfo.disposal == 3 && restoreCanvas.size() == canvas.size()) canvas = restoreCanvas;
                restoreCanvas.clear();
            }
            if (info.disposal == 3) restoreCanvas = canvas;
            CompositeFrame(canvas, canvasWidth, canvasHeight, info, framePixels);

            DecodedGifAnimation::Frame decoded;
            decoded.duration = FrameDelay(frame.Get());
            if (FAILED(context->CreateBitmap(
                    D2D1::SizeU(canvasWidth, canvasHeight),
                    canvas.data(),
                    canvasWidth * 4u,
                    &properties,
                    decoded.bitmap.GetAddressOf())) || !decoded.bitmap) return std::nullopt;
            result.cycle += decoded.duration;
            result.frames.push_back(std::move(decoded));
            previousInfo = info;
        }
        if (result.frames.size() != frameCount || result.cycle.count() <= 0) return std::nullopt;
        return result;
    }
}

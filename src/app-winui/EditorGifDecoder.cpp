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

    void FillCanvas(std::vector<std::uint8_t>& canvas, std::array<std::uint8_t, 4> const& pixel)
    {
        for (auto offset = std::size_t{0}; offset < canvas.size(); offset += 4)
            std::copy(pixel.begin(), pixel.end(), canvas.begin() + offset);
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
    struct DecodedGifAnimation::State
    {
        struct Frame
        {
            GifFrameInfo info;
            std::chrono::milliseconds duration{100};
            std::chrono::milliseconds boundary{0};
        };

        ::Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        std::shared_ptr<std::vector<std::uint8_t> const> encodedBacking;
        std::vector<Frame> frames;
        std::vector<std::uint8_t> canvas;
        std::vector<std::uint8_t> restoreCanvas;
        std::array<std::uint8_t, 4> background{};
        std::chrono::milliseconds cycle{0};
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        std::uint64_t currentCycle = 0;
        std::size_t currentFrame = 0;
        std::size_t memoryCost = 0;
        UINT width = 0;
        UINT height = 0;
        bool failed = false;

        bool DecodeAndComposite(std::size_t index)
        {
            ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            if (index >= frames.size()
                || FAILED(decoder->GetFrame(static_cast<UINT>(index), frame.GetAddressOf()))
                || !frame) return false;
            auto const& info = frames[index].info;
            auto stride = info.width * 4u;
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * info.height);
            ::Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
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
                    static_cast<UINT>(pixels.size()),
                    pixels.data()))) return false;
            CompositeFrame(canvas, width, height, info, pixels);
            return true;
        }

        bool AdvanceOne()
        {
            if (currentFrame + 1 >= frames.size()) return false;
            auto const& previous = frames[currentFrame].info;
            if (previous.disposal == 2) FillRect(canvas, width, height, previous, background);
            else if (previous.disposal == 3 && restoreCanvas.size() == canvas.size()) canvas = restoreCanvas;
            restoreCanvas.clear();
            ++currentFrame;
            if (frames[currentFrame].info.disposal == 3) restoreCanvas = canvas;
            return DecodeAndComposite(currentFrame);
        }

        bool ResetTo(std::size_t target)
        {
            FillCanvas(canvas, background);
            restoreCanvas.clear();
            currentFrame = 0;
            if (frames.front().info.disposal == 3) restoreCanvas = canvas;
            if (!DecodeAndComposite(0)) return false;
            while (currentFrame < target)
                if (!AdvanceOne()) return false;
            return true;
        }

        bool Upload()
        {
            return bitmap && SUCCEEDED(bitmap->CopyFromMemory(nullptr, canvas.data(), width * 4u));
        }
    };

    DecodedGifAnimation::DecodedGifAnimation(std::shared_ptr<State> value)
        : state(std::move(value))
    {
    }

    ID2D1Bitmap1* DecodedGifAnimation::CurrentBitmap(std::chrono::milliseconds& untilNextFrame)
    {
        untilNextFrame = std::chrono::milliseconds{0};
        if (!state || state->failed || !state->bitmap || state->frames.size() < 2 || state->cycle.count() <= 0)
            return state ? state->bitmap.Get() : nullptr;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - state->started);
        auto cycleNumber = static_cast<std::uint64_t>(elapsed.count() / state->cycle.count());
        auto position = std::chrono::milliseconds{elapsed.count() % state->cycle.count()};
        auto target = std::size_t{0};
        while (target + 1 < state->frames.size() && position >= state->frames[target].boundary) ++target;
        untilNextFrame = (std::max)(
            std::chrono::milliseconds{1},
            state->frames[target].boundary - position);
        auto changed = false;
        if (cycleNumber != state->currentCycle || target < state->currentFrame)
        {
            changed = true;
            state->currentCycle = cycleNumber;
            if (!state->ResetTo(target)) state->failed = true;
        }
        else
        {
            while (!state->failed && state->currentFrame < target)
            {
                changed = true;
                if (!state->AdvanceOne()) state->failed = true;
            }
        }
        if (changed && !state->failed && !state->Upload()) state->failed = true;
        if (state->failed) untilNextFrame = std::chrono::milliseconds{0};
        return state->bitmap.Get();
    }

    ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> const& DecodedGifAnimation::Bitmap() const
    {
        return state->bitmap;
    }

    UINT DecodedGifAnimation::Width() const { return state ? state->width : 0; }
    UINT DecodedGifAnimation::Height() const { return state ? state->height : 0; }
    std::size_t DecodedGifAnimation::MemoryCost() const { return state ? state->memoryCost : 0; }

    std::shared_ptr<DecodedGifAnimation> DecodeGifAnimation(
        IWICImagingFactory* factory,
        ID2D1DeviceContext* context,
        IWICBitmapDecoder* decoder,
        std::shared_ptr<std::vector<std::uint8_t> const> encodedBacking,
        std::size_t runtimeByteBudget)
    {
        if (!factory || !context || !decoder) return {};
        GUID containerFormat{};
        UINT frameCount = 0;
        if (FAILED(decoder->GetContainerFormat(&containerFormat))
            || !IsEqualGUID(containerFormat, GUID_ContainerFormatGif)
            || FAILED(decoder->GetFrameCount(&frameCount))
            || frameCount < 2
            || frameCount > 4096) return {};

        ::Microsoft::WRL::ComPtr<IWICMetadataQueryReader> decoderMetadata;
        decoder->GetMetadataQueryReader(decoderMetadata.GetAddressOf());
        auto width = MetadataUnsigned(decoderMetadata.Get(), L"/logscrdesc/Width").value_or(0);
        auto height = MetadataUnsigned(decoderMetadata.Get(), L"/logscrdesc/Height").value_or(0);
        ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> firstFrame;
        if (FAILED(decoder->GetFrame(0, firstFrame.GetAddressOf())) || !firstFrame) return {};
        auto firstInfo = ReadFrameInfo(firstFrame.Get());
        if (width == 0) width = firstInfo.left + firstInfo.width;
        if (height == 0) height = firstInfo.top + firstInfo.height;
        auto canvasPixels = static_cast<std::uint64_t>(width) * height;
        auto canvasBytes = canvasPixels * 4ull;
        auto backingBytes = encodedBacking ? encodedBacking->size() : 0;
        auto runtimeBytes = canvasBytes * 2ull + backingBytes
            + static_cast<std::uint64_t>(frameCount) * sizeof(DecodedGifAnimation::State::Frame);
        if (width == 0 || height == 0
            || canvasPixels > 16ull * 1024ull * 1024ull
            || runtimeBytes > runtimeByteBudget) return {};

        auto state = std::make_shared<DecodedGifAnimation::State>();
        state->factory = factory;
        state->decoder = decoder;
        state->encodedBacking = std::move(encodedBacking);
        state->width = width;
        state->height = height;
        state->memoryCost = static_cast<std::size_t>(runtimeBytes);
        state->background = BackgroundPixel(factory, decoder, firstInfo);
        state->canvas.resize(static_cast<std::size_t>(canvasBytes));
        state->frames.reserve(frameCount);
        for (UINT index = 0; index < frameCount; ++index)
        {
            ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(index, frame.GetAddressOf())) || !frame) return {};
            auto info = ReadFrameInfo(frame.Get());
            if (info.width == 0 || info.height == 0
                || info.left >= width || info.top >= height
                || static_cast<std::uint64_t>(info.width) * info.height > 16ull * 1024ull * 1024ull) return {};
            auto duration = FrameDelay(frame.Get());
            state->cycle += duration;
            state->frames.push_back(DecodedGifAnimation::State::Frame{ info, duration, state->cycle });
        }
        FillCanvas(state->canvas, state->background);
        if (state->frames.front().info.disposal == 3) state->restoreCanvas = state->canvas;
        if (!state->DecodeAndComposite(0)) return {};
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        if (FAILED(context->CreateBitmap(
                D2D1::SizeU(width, height),
                state->canvas.data(),
                width * 4u,
                &properties,
                state->bitmap.GetAddressOf())) || !state->bitmap) return {};
        state->started = std::chrono::steady_clock::now();
        return std::shared_ptr<DecodedGifAnimation>(new DecodedGifAnimation(std::move(state)));
    }
}

#include "pch.h"
#include "media/EditorGifDecoder.h"

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

    std::shared_ptr<winrt::ElMd::GifInitialFrame> DecodeInitialFrame(
        IWICImagingFactory* factory,
        IWICBitmapDecoder* decoder,
        std::size_t backingBytes,
        std::size_t runtimeByteBudget)
    {
        if (!factory || !decoder) return {};
        GUID containerFormat{};
        UINT frameCount = 0;
        if (FAILED(decoder->GetContainerFormat(&containerFormat))
            || !IsEqualGUID(containerFormat, GUID_ContainerFormatGif)
            || FAILED(decoder->GetFrameCount(&frameCount))
            || frameCount == 0
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
        auto runtimeBytes = canvasBytes * 2ull + backingBytes
            + static_cast<std::uint64_t>(frameCount) * 64ull;
        if (width == 0 || height == 0
            || canvasPixels > 16ull * 1024ull * 1024ull
            || runtimeBytes > runtimeByteBudget) return {};

        auto result = std::make_shared<winrt::ElMd::GifInitialFrame>();
        result->frameCount = frameCount;
        result->width = width;
        result->height = height;
        result->firstLeft = firstInfo.left;
        result->firstTop = firstInfo.top;
        result->firstWidth = firstInfo.width;
        result->firstHeight = firstInfo.height;
        result->firstDisposal = firstInfo.disposal;
        result->firstTransparent = firstInfo.transparent;
        result->firstTransparentIndex = firstInfo.transparentIndex;
        result->firstDuration = FrameDelay(firstFrame.Get());
        result->background = BackgroundPixel(factory, decoder, firstInfo);
        result->canvas.resize(static_cast<std::size_t>(canvasBytes));
        FillCanvas(result->canvas, result->background);
        ::Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        auto stride = firstInfo.width * 4u;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * firstInfo.height);
        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))
            || FAILED(converter->Initialize(
                firstFrame.Get(),
                GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeMedianCut))
            || FAILED(converter->CopyPixels(
                nullptr,
                stride,
                static_cast<UINT>(pixels.size()),
                pixels.data()))) return {};
        CompositeFrame(result->canvas, width, height, firstInfo, pixels);
        return result;
    }
}

namespace winrt::ElMd
{
    bool GifInitialDecode::Complete() const
    {
        std::scoped_lock lock(mutex);
        return complete;
    }

    bool GifInitialDecode::Failed() const
    {
        std::scoped_lock lock(mutex);
        return complete && failed;
    }

    void GifInitialDecode::Cancel()
    {
        std::scoped_lock lock(mutex);
        cancelled = true;
        result.reset();
    }

    std::shared_ptr<GifInitialFrame const> GifInitialDecode::Result() const
    {
        std::scoped_lock lock(mutex);
        return result;
    }

    std::shared_ptr<GifInitialDecode> QueueGifInitialDecode(
        std::filesystem::path path,
        std::shared_ptr<std::vector<std::uint8_t>> encodedBacking,
        std::function<void()> completion,
        std::size_t runtimeByteBudget)
    {
        auto pending = std::make_shared<GifInitialDecode>();
        std::thread([pending, path = std::move(path), encodedBacking = std::move(encodedBacking), completion = std::move(completion), runtimeByteBudget]() mutable
        {
            auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            std::shared_ptr<GifInitialFrame> result;
            try
            {
                ::Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
                ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
                ::Microsoft::WRL::ComPtr<IWICStream> stream;
                if (SUCCEEDED(CoCreateInstance(
                        CLSID_WICImagingFactory,
                        nullptr,
                        CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(factory.GetAddressOf()))) && factory)
                {
                    if (encodedBacking && !encodedBacking->empty()
                        && encodedBacking->size() <= (std::numeric_limits<DWORD>::max)()
                        && SUCCEEDED(factory->CreateStream(stream.GetAddressOf()))
                        && SUCCEEDED(stream->InitializeFromMemory(
                            encodedBacking->data(),
                            static_cast<DWORD>(encodedBacking->size())))
                        && SUCCEEDED(factory->CreateDecoderFromStream(
                            stream.Get(),
                            nullptr,
                            WICDecodeMetadataCacheOnLoad,
                            decoder.GetAddressOf())))
                    {
                        result = DecodeInitialFrame(factory.Get(), decoder.Get(), encodedBacking->size(), runtimeByteBudget);
                    }
                    else if ((!encodedBacking || encodedBacking->empty())
                        && !path.empty()
                        && SUCCEEDED(factory->CreateDecoderFromFilename(
                            path.c_str(),
                            nullptr,
                            GENERIC_READ,
                            WICDecodeMetadataCacheOnLoad,
                            decoder.GetAddressOf())))
                    {
                        result = DecodeInitialFrame(factory.Get(), decoder.Get(), 0, runtimeByteBudget);
                    }
                }
            }
            catch (...) {}
            auto notify = false;
            {
                std::scoped_lock lock(pending->mutex);
                if (!pending->cancelled)
                {
                    pending->result = std::move(result);
                    pending->failed = !pending->result;
                    notify = true;
                }
                pending->complete = true;
            }
            if (notify && completion) completion();
            if (SUCCEEDED(initialized)) CoUninitialize();
        }).detach();
        return pending;
    }

    struct DecodedGifAnimation::State
    {
        struct Frame
        {
            GifFrameInfo info;
            std::chrono::milliseconds duration{100};
            std::chrono::milliseconds boundary{0};
        };

        struct Worker
        {
            mutable std::mutex mutex;
            std::condition_variable wake;
            std::filesystem::path sourcePath;
            std::shared_ptr<std::vector<std::uint8_t> const> encodedBacking;
            std::function<void()> completion;
            std::vector<std::optional<Frame>> frames;
            std::vector<std::uint8_t> canvas;
            std::vector<std::uint8_t> restoreCanvas;
            std::array<std::uint8_t, 4> background{};
            std::shared_ptr<std::vector<std::uint8_t> const> publishedCanvas;
            std::chrono::milliseconds publishedDuration{100};
            std::uint64_t generation = 1;
            std::uint64_t publication = 0;
            std::size_t currentFrame = 0;
            UINT width = 0;
            UINT height = 0;
            bool requested = false;
            bool requestPending = false;
            bool stopped = false;
            bool failed = false;

            static bool EnsureFrame(
                IWICBitmapDecoder* decoder,
                std::vector<std::optional<Frame>>& frames,
                std::size_t index,
                UINT width,
                UINT height)
            {
                if (!decoder || index >= frames.size()) return false;
                if (frames[index]) return true;
                ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
                if (FAILED(decoder->GetFrame(static_cast<UINT>(index), frame.GetAddressOf())) || !frame) return false;
                auto info = ReadFrameInfo(frame.Get());
                if (info.width == 0 || info.height == 0
                    || info.left >= width || info.top >= height
                    || static_cast<std::uint64_t>(info.width) * info.height > 16ull * 1024ull * 1024ull) return false;
                frames[index] = Frame{ info, FrameDelay(frame.Get()), std::chrono::milliseconds{0} };
                return true;
            }

            static bool DecodeAndComposite(
                IWICImagingFactory* factory,
                IWICBitmapDecoder* decoder,
                std::vector<std::optional<Frame>>& frames,
                std::vector<std::uint8_t>& canvas,
                std::size_t index,
                UINT width,
                UINT height)
            {
                ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
                if (!EnsureFrame(decoder, frames, index, width, height)
                    || FAILED(decoder->GetFrame(static_cast<UINT>(index), frame.GetAddressOf()))
                    || !frame) return false;
                auto const& info = frames[index]->info;
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

            static bool AdvanceOne(
                IWICImagingFactory* factory,
                IWICBitmapDecoder* decoder,
                std::vector<std::optional<Frame>>& frames,
                std::vector<std::uint8_t>& canvas,
                std::vector<std::uint8_t>& restoreCanvas,
                std::array<std::uint8_t, 4> const& background,
                std::size_t& currentFrame,
                UINT width,
                UINT height)
            {
                if (frames.empty() || !EnsureFrame(decoder, frames, currentFrame, width, height)) return false;
                auto next = (currentFrame + 1) % frames.size();
                if (next == 0)
                {
                    FillCanvas(canvas, background);
                    restoreCanvas.clear();
                    currentFrame = 0;
                    if (!EnsureFrame(decoder, frames, 0, width, height)) return false;
                    if (frames[0]->info.disposal == 3) restoreCanvas = canvas;
                    return DecodeAndComposite(factory, decoder, frames, canvas, 0, width, height);
                }
                auto const& previous = frames[currentFrame]->info;
                if (previous.disposal == 2) FillRect(canvas, width, height, previous, background);
                else if (previous.disposal == 3 && restoreCanvas.size() == canvas.size()) canvas = restoreCanvas;
                restoreCanvas.clear();
                currentFrame = next;
                if (!EnsureFrame(decoder, frames, currentFrame, width, height)) return false;
                if (frames[currentFrame]->info.disposal == 3) restoreCanvas = canvas;
                return DecodeAndComposite(factory, decoder, frames, canvas, currentFrame, width, height);
            }

            void Request()
            {
                std::scoped_lock lock(mutex);
                if (stopped || failed || requestPending) return;
                requestPending = true;
                requested = true;
                wake.notify_one();
            }

            void Stop()
            {
                std::scoped_lock lock(mutex);
                stopped = true;
                ++generation;
                publishedCanvas.reset();
                wake.notify_one();
            }

            void Run()
            {
                auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                ::Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
                ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
                ::Microsoft::WRL::ComPtr<IWICStream> stream;
                auto ready = SUCCEEDED(CoCreateInstance(
                    CLSID_WICImagingFactory,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(factory.GetAddressOf()))) && factory;
                if (ready && encodedBacking && !encodedBacking->empty()
                    && encodedBacking->size() <= (std::numeric_limits<DWORD>::max)())
                {
                    ready = SUCCEEDED(factory->CreateStream(stream.GetAddressOf()))
                        && SUCCEEDED(stream->InitializeFromMemory(
                            const_cast<std::uint8_t*>(encodedBacking->data()),
                            static_cast<DWORD>(encodedBacking->size())))
                        && SUCCEEDED(factory->CreateDecoderFromStream(
                            stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
                }
                else if (ready && !sourcePath.empty())
                {
                    ready = SUCCEEDED(factory->CreateDecoderFromFilename(
                        sourcePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
                }
                else ready = false;

                while (ready)
                {
                    std::uint64_t requestGeneration = 0;
                    {
                        std::unique_lock lock(mutex);
                        wake.wait(lock, [this] { return stopped || requested; });
                        if (stopped) break;
                        requested = false;
                        requestGeneration = generation;
                    }
                    auto advanced = AdvanceOne(
                        factory.Get(), decoder.Get(), frames, canvas, restoreCanvas,
                        background, currentFrame, width, height);
                    std::shared_ptr<std::vector<std::uint8_t> const> snapshot;
                    auto duration = std::chrono::milliseconds{100};
                    if (advanced)
                    {
                        snapshot = std::make_shared<std::vector<std::uint8_t>>(canvas);
                        duration = frames[currentFrame]->duration;
                    }
                    auto notify = false;
                    {
                        std::scoped_lock lock(mutex);
                        if (stopped || requestGeneration != generation) continue;
                        requestPending = false;
                        if (!advanced)
                        {
                            failed = true;
                        }
                        else
                        {
                            publishedCanvas = std::move(snapshot);
                            publishedDuration = duration;
                            ++publication;
                            notify = true;
                        }
                    }
                    if (notify && completion) completion();
                    if (!advanced) break;
                }
                if (!ready)
                {
                    std::scoped_lock lock(mutex);
                    if (!stopped)
                    {
                        requestPending = false;
                        failed = true;
                    }
                }
                stream.Reset();
                decoder.Reset();
                factory.Reset();
                if (SUCCEEDED(initialized)) CoUninitialize();
            }
        };

        ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        std::shared_ptr<Worker> worker;
        std::chrono::steady_clock::time_point nextFrameAt = std::chrono::steady_clock::now();
        std::uint64_t seenPublication = 0;
        std::size_t frameCount = 0;
        std::size_t memoryCost = 0;
        UINT width = 0;
        UINT height = 0;
        bool failed = false;
    };

    DecodedGifAnimation::DecodedGifAnimation(std::shared_ptr<State> value)
        : state(std::move(value))
    {
    }

    DecodedGifAnimation::~DecodedGifAnimation()
    {
        if (state && state->worker) state->worker->Stop();
    }

    ID2D1Bitmap1* DecodedGifAnimation::CurrentBitmap(std::chrono::milliseconds& untilNextFrame)
    {
        untilNextFrame = std::chrono::milliseconds{0};
        if (!state || state->failed || !state->bitmap || state->frameCount < 2 || !state->worker)
            return state ? state->bitmap.Get() : nullptr;
        auto now = std::chrono::steady_clock::now();
        std::shared_ptr<std::vector<std::uint8_t> const> published;
        auto duration = std::chrono::milliseconds{100};
        {
            std::scoped_lock lock(state->worker->mutex);
            if (state->worker->failed) state->failed = true;
            if (state->worker->publication != state->seenPublication)
            {
                state->seenPublication = state->worker->publication;
                published = state->worker->publishedCanvas;
                duration = state->worker->publishedDuration;
            }
        }
        if (published)
        {
            if (FAILED(state->bitmap->CopyFromMemory(nullptr, published->data(), state->width * 4u)))
                state->failed = true;
            else
                state->nextFrameAt = now + duration;
        }
        if (!state->failed && now >= state->nextFrameAt)
        {
            state->worker->Request();
            untilNextFrame = std::chrono::milliseconds{0};
        }
        else if (!state->failed)
            untilNextFrame = (std::max)(
                std::chrono::milliseconds{1},
                std::chrono::duration_cast<std::chrono::milliseconds>(state->nextFrameAt - now));
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
        std::shared_ptr<GifInitialFrame const> initialFrame,
        std::filesystem::path sourcePath,
        std::function<void()> frameCompletion,
        std::size_t runtimeByteBudget)
    {
        if (!factory || !context || !decoder) return {};
        if (!initialFrame)
            initialFrame = DecodeInitialFrame(
                factory,
                decoder,
                encodedBacking ? encodedBacking->size() : 0,
                runtimeByteBudget);
        if (!initialFrame || initialFrame->frameCount == 0) return {};
        auto width = initialFrame->width;
        auto height = initialFrame->height;
        auto frameCount = initialFrame->frameCount;
        auto firstInfo = GifFrameInfo{
            initialFrame->firstLeft,
            initialFrame->firstTop,
            initialFrame->firstWidth,
            initialFrame->firstHeight,
            initialFrame->firstDisposal,
            initialFrame->firstTransparent,
            initialFrame->firstTransparentIndex,
        };
        auto canvasPixels = static_cast<std::uint64_t>(width) * height;
        auto canvasBytes = canvasPixels * 4ull;
        auto backingBytes = encodedBacking ? encodedBacking->size() : 0;
        auto runtimeBytes = canvasBytes * 2ull + backingBytes
            + static_cast<std::uint64_t>(frameCount) * sizeof(DecodedGifAnimation::State::Frame);
        if (width == 0 || height == 0
            || canvasPixels > 16ull * 1024ull * 1024ull
            || initialFrame->canvas.size() != canvasBytes
            || runtimeBytes > runtimeByteBudget) return {};

        auto state = std::make_shared<DecodedGifAnimation::State>();
        state->width = width;
        state->height = height;
        state->frameCount = frameCount;
        state->memoryCost = static_cast<std::size_t>(runtimeBytes);
        auto worker = std::make_shared<DecodedGifAnimation::State::Worker>();
        worker->sourcePath = std::move(sourcePath);
        worker->encodedBacking = std::move(encodedBacking);
        worker->completion = std::move(frameCompletion);
        worker->width = width;
        worker->height = height;
        worker->background = initialFrame->background;
        worker->canvas = initialFrame->canvas;
        worker->frames.resize(frameCount);
        worker->frames[0] = DecodedGifAnimation::State::Frame{
            firstInfo,
            initialFrame->firstDuration,
            std::chrono::milliseconds{0},
        };
        if (worker->frames.front()->info.disposal == 3)
        {
            worker->restoreCanvas.resize(static_cast<std::size_t>(canvasBytes));
            FillCanvas(worker->restoreCanvas, worker->background);
        }
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        if (FAILED(context->CreateBitmap(
                D2D1::SizeU(width, height),
                initialFrame->canvas.data(),
                width * 4u,
                &properties,
                state->bitmap.GetAddressOf())) || !state->bitmap) return {};
        state->worker = worker;
        state->nextFrameAt = std::chrono::steady_clock::now() + initialFrame->firstDuration;
        if (frameCount > 1)
            std::thread([worker] { worker->Run(); }).detach();
        return std::shared_ptr<DecodedGifAnimation>(new DecodedGifAnimation(std::move(state)));
    }
}

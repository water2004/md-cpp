#include "pch.h"
#include "editor/rendering/EditorRenderCache.h"
#include "media/EditorGifDecoder.h"

import elmd.core.render_model;
import elmd.core.types;

#include "editor/rendering/EditorContentPreparation.h"

namespace
{
    bool HasGifMagic(std::vector<std::uint8_t> const& bytes)
    {
        if (bytes.size() < 6) return false;
        auto header = std::string_view(reinterpret_cast<char const*>(bytes.data()), 6);
        return header == "GIF87a" || header == "GIF89a";
    }

    bool HasGifExtension(std::filesystem::path const& path)
    {
        auto extension = path.extension().wstring();
        return _wcsicmp(extension.c_str(), L".gif") == 0;
    }

    std::optional<winrt::ElMd::EditorRenderCache::ImageDimensions> GifDimensionsFromHeader(
        std::uint8_t const* bytes,
        std::size_t size)
    {
        if (!bytes || size < 10) return std::nullopt;
        auto signature = std::string_view(reinterpret_cast<char const*>(bytes), 6);
        if (signature != "GIF87a" && signature != "GIF89a") return std::nullopt;
        auto width = static_cast<UINT>(bytes[6]) | (static_cast<UINT>(bytes[7]) << 8);
        auto height = static_cast<UINT>(bytes[8]) | (static_cast<UINT>(bytes[9]) << 8);
        if (width == 0 || height == 0
            || static_cast<std::uint64_t>(width) * height > 16ull * 1024ull * 1024ull)
            return std::nullopt;
        return winrt::ElMd::EditorRenderCache::ImageDimensions{
            static_cast<float>(width),
            static_cast<float>(height),
        };
    }

    std::optional<winrt::ElMd::EditorRenderCache::ImageDimensions> GifDimensionsFromBase64(
        std::string_view source)
    {
        static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::array<std::uint8_t, 10> header{};
        auto count = std::size_t{0};
        std::uint32_t accumulator = 0;
        unsigned bits = 0;
        for (auto ch : source)
        {
            if (ch == '=') break;
            if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
            auto position = alphabet.find(ch);
            if (position == std::string_view::npos) return std::nullopt;
            accumulator = (accumulator << 6) | static_cast<std::uint32_t>(position);
            bits += 6;
            if (bits < 8) continue;
            bits -= 8;
            header[count++] = static_cast<std::uint8_t>((accumulator >> bits) & 0xff);
            if (count == header.size()) break;
        }
        return GifDimensionsFromHeader(header.data(), count);
    }

    std::optional<std::wstring> RasterImageKey(
        std::wstring const& baseDirectory,
        std::string_view source)
    {
        if (source.empty()) return std::nullopt;
        if (source.starts_with("https://") || source.starts_with("http://"))
            return L"url:" + std::wstring(winrt::to_hstring(std::string(source)));
        if (source.starts_with("data:image/"))
            return L"data:" + std::to_wstring(source.size()) + L":"
                + std::to_wstring(std::hash<std::string_view>{}(source));
        auto sourceText = winrt::to_hstring(std::string(source));
        auto path = std::filesystem::path(sourceText.c_str());
        if (path.has_root_directory() && !path.has_root_name() && !baseDirectory.empty())
            path = std::filesystem::path(baseDirectory) / path.relative_path();
        if (path.is_relative())
        {
            if (baseDirectory.empty()) return std::nullopt;
            path = std::filesystem::path(baseDirectory) / path;
        }
        std::error_code error;
        auto absolute = std::filesystem::weakly_canonical(path, error);
        if (error) absolute = path.lexically_normal();
        return absolute.wstring();
    }
}

namespace winrt::ElMd
{
    std::optional<EditorRenderCache::ImageDimensions> EditorRenderCache::ProbeGifDimensions(
        std::wstring const& baseDirectory,
        std::string_view source)
    {
        auto key = RasterImageKey(baseDirectory, source);
        if (!key) return std::nullopt;
        if (auto found = gifDimensions.find(*key); found != gifDimensions.end()) return found->second;
        if (gifDimensionMisses.contains(*key)) return std::nullopt;
        if (auto found = rasterImages.find(*key); found != rasterImages.end() && found->second.animation)
            return ImageDimensions{found->second.width, found->second.height};

        std::optional<ImageDimensions> dimensions;
        if (source.starts_with("data:image/"))
        {
            auto comma = source.find(',');
            if (comma != std::string_view::npos
                && source.substr(0, comma).find(";base64") != std::string_view::npos)
                dimensions = GifDimensionsFromBase64(source.substr(comma + 1));
        }
        else if (source.starts_with("https://") || source.starts_with("http://"))
        {
            auto remoteSource = std::string(source);
            {
                std::scoped_lock lock(remoteState->mutex);
                if (auto found = remoteState->dimensions.find(remoteSource); found != remoteState->dimensions.end())
                    dimensions = found->second;
                else if (auto dataFound = remoteState->data.find(remoteSource); dataFound != remoteState->data.end())
                    dimensions = GifDimensionsFromHeader(dataFound->second.data(), dataFound->second.size());
            }
            if (!dimensions) QueueRemoteGifDimensions(std::move(remoteSource));
        }
        else
        {
            auto path = std::filesystem::path(*key);
            std::array<std::uint8_t, 10> header{};
            std::ifstream stream(path, std::ios::binary);
            if (stream)
            {
                stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
                dimensions = GifDimensionsFromHeader(header.data(), static_cast<std::size_t>(stream.gcount()));
            }
        }
        if (!dimensions)
        {
            // Remote metadata is asynchronous; unlike local/data misses it
            // must remain probeable after the range request completes.
            if (!source.starts_with("https://") && !source.starts_with("http://"))
            {
                if (gifDimensionMisses.size() >= 2048) gifDimensionMisses.clear();
                gifDimensionMisses.insert(*key);
            }
            return std::nullopt;
        }
        constexpr std::size_t dimensionLimit = 1024;
        while (gifDimensionOrder.size() >= dimensionLimit)
        {
            gifDimensions.erase(gifDimensionOrder.front());
            gifDimensionOrder.pop_front();
        }
        gifDimensionOrder.push_back(*key);
        gifDimensions.emplace(*key, *dimensions);
        return dimensions;
    }

    ID2D1Bitmap1* EditorRenderCache::CurrentBitmap(
        RasterImage const& image,
        std::chrono::milliseconds& untilNextFrame) const
    {
        untilNextFrame = std::chrono::milliseconds{0};
        return image.animation
            ? image.animation->CurrentBitmap(untilNextFrame)
            : image.bitmap.Get();
    }

    void EditorRenderCache::RequestAnimationFrame(std::chrono::milliseconds delay)
    {
        if (delay.count() <= 0) return;
        delay = (std::clamp)(delay, std::chrono::milliseconds{1}, std::chrono::milliseconds{10000});
        auto deadline = std::chrono::steady_clock::now() + delay;
        if (!animationDeadline || deadline < *animationDeadline) animationDeadline = deadline;
        if (animationPumpActive) return;
        animationPumpActive = true;
        animationRenderingToken = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
            [this](auto const&, auto const&)
            {
                if (!animationDeadline)
                {
                    StopAnimationPump();
                    return;
                }
                if (std::chrono::steady_clock::now() < *animationDeadline) return;
                animationDeadline.reset();
                std::function<void()> invalidate;
                {
                    std::scoped_lock lock(remoteState->mutex);
                    if (remoteState->active) invalidate = remoteState->invalidate;
                }
                if (invalidate) invalidate();
                // A visible animated image requests its following deadline
                // synchronously while the invalidated frame is drawn. If no
                // image did so, it has left the viewport and the pump can stop.
                if (!animationDeadline) StopAnimationPump();
            });
    }

    void EditorRenderCache::StopAnimationPump()
    {
        if (animationPumpActive)
            winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(animationRenderingToken);
        animationRenderingToken = {};
        animationDeadline.reset();
        animationPumpActive = false;
    }

    void EditorRenderCache::QueueRemoteGifDimensions(std::string source)
    {
        auto state = remoteState;
        {
            std::scoped_lock lock(state->mutex);
            if (!state->active
                || state->dimensions.contains(source)
                || state->dimensionPending.contains(source)
                || state->dimensionFailed.contains(source)) return;
            state->dimensionPending.insert(source);
        }
        auto publish = [state, source](std::optional<ImageDimensions> dimensions)
        {
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
            {
                std::scoped_lock lock(state->mutex);
                state->dimensionPending.erase(source);
                if (!state->active) return;
                if (dimensions)
                {
                    constexpr std::size_t dimensionLimit = 1024;
                    while (state->dimensionOrder.size() >= dimensionLimit)
                    {
                        state->dimensions.erase(state->dimensionOrder.front());
                        state->dimensionOrder.pop_front();
                    }
                    if (!state->dimensions.contains(source)) state->dimensionOrder.push_back(source);
                    state->dimensions.insert_or_assign(source, *dimensions);
                }
                else
                {
                    state->dimensionFailed.insert(source);
                }
                ++state->generation;
                dispatcher = state->dispatcher;
            }
            if (dispatcher)
            {
                dispatcher.TryEnqueue([state]
                {
                    std::function<void()> invalidate;
                    {
                        std::scoped_lock lock(state->mutex);
                        if (state->active) invalidate = state->invalidate;
                    }
                    if (invalidate) invalidate();
                });
            }
        };
        try
        {
            auto client = winrt::Windows::Web::Http::HttpClient{};
            auto request = winrt::Windows::Web::Http::HttpRequestMessage{
                winrt::Windows::Web::Http::HttpMethod::Get(),
                winrt::Windows::Foundation::Uri(winrt::to_hstring(source)),
            };
            request.Headers().TryAppendWithoutValidation(L"Range", L"bytes=0-9");
            auto operation = client.SendRequestAsync(
                request,
                winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
            operation.Completed([client, publish](auto const& async, winrt::Windows::Foundation::AsyncStatus status) mutable
            {
                if (status != winrt::Windows::Foundation::AsyncStatus::Completed)
                {
                    publish(std::nullopt);
                    return;
                }
                try
                {
                    auto response = async.GetResults();
                    if (!response.IsSuccessStatusCode())
                    {
                        publish(std::nullopt);
                        return;
                    }
                    auto streamOperation = response.Content().ReadAsInputStreamAsync();
                    streamOperation.Completed([response, publish](auto const& streamAsync, winrt::Windows::Foundation::AsyncStatus streamStatus) mutable
                    {
                        if (streamStatus != winrt::Windows::Foundation::AsyncStatus::Completed)
                        {
                            publish(std::nullopt);
                            return;
                        }
                        try
                        {
                            auto reader = winrt::Windows::Storage::Streams::DataReader{streamAsync.GetResults()};
                            reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);
                            auto loadOperation = reader.LoadAsync(10);
                            loadOperation.Completed([response, reader, publish](auto const& loadAsync, winrt::Windows::Foundation::AsyncStatus loadStatus) mutable
                            {
                                if (loadStatus != winrt::Windows::Foundation::AsyncStatus::Completed)
                                {
                                    publish(std::nullopt);
                                    return;
                                }
                                try
                                {
                                    auto count = (std::min)(loadAsync.GetResults(), 10u);
                                    std::array<std::uint8_t, 10> header{};
                                    reader.ReadBytes(winrt::array_view<std::uint8_t>(header.data(), header.data() + count));
                                    publish(GifDimensionsFromHeader(header.data(), count));
                                }
                                catch (...)
                                {
                                    publish(std::nullopt);
                                }
                            });
                        }
                        catch (...)
                        {
                            publish(std::nullopt);
                        }
                    });
                }
                catch (...)
                {
                    publish(std::nullopt);
                }
            });
        }
        catch (...)
        {
            publish(std::nullopt);
        }
    }

    void EditorRenderCache::QueueRemoteImage(std::string source)
    {
        auto state = remoteState;
        {
            std::scoped_lock lock(state->mutex);
            if (!state->active || state->data.contains(source) || state->pending.contains(source) || state->failed.contains(source)) return;
            state->pending.insert(source);
        }
        try
        {
            auto operation = winrt::Windows::Web::Http::HttpClient{}.GetBufferAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(source)));
            operation.Completed([state, source = std::move(source)](auto const& async, winrt::Windows::Foundation::AsyncStatus status)
            {
                std::vector<std::uint8_t> bytes;
                if (status == winrt::Windows::Foundation::AsyncStatus::Completed)
                {
                    try
                    {
                        auto buffer = async.GetResults();
                        if (buffer.Length() <= 16 * 1024 * 1024)
                        {
                            bytes.resize(buffer.Length());
                            auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
                            reader.ReadBytes(winrt::array_view<std::uint8_t>(bytes));
                        }
                    }
                    catch (...) {}
                }
                winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
                {
                    std::scoped_lock lock(state->mutex);
                    state->pending.erase(source);
                    if (!state->active) return;
                    if (bytes.empty())
                    {
                        state->failed.insert(source);
                    }
                    else
                    {
                        while (!state->order.empty() && (state->data.size() >= 16 || state->bytes + bytes.size() > 32 * 1024 * 1024))
                        {
                            auto oldest = std::move(state->order.front());
                            state->order.pop_front();
                            auto found = state->data.find(oldest);
                            if (found == state->data.end()) continue;
                            state->bytes -= found->second.size();
                            state->data.erase(found);
                        }
                        if (bytes.size() <= 32 * 1024 * 1024)
                        {
                            state->bytes += bytes.size();
                            state->order.push_back(source);
                            state->data.emplace(source, std::move(bytes));
                        }
                    }
                    ++state->generation;
                    dispatcher = state->dispatcher;
                }
                if (dispatcher)
                {
                    dispatcher.TryEnqueue([state]
                    {
                        std::function<void()> invalidate;
                        {
                            std::scoped_lock lock(state->mutex);
                            if (state->active) invalidate = state->invalidate;
                        }
                        if (invalidate) invalidate();
                    });
                }
            });
        }
        catch (...)
        {
            std::scoped_lock lock(state->mutex);
            state->pending.erase(source);
            if (!state->active) return;
            state->failed.insert(std::move(source));
            ++state->generation;
        }
    }

    std::optional<EditorRenderCache::RasterImage> EditorRenderCache::LoadRasterImage(EditorRenderResources const& resources, std::wstring const& baseDirectory, std::string_view source)
    {
        if (!resources.wicFactory || !resources.d2dContext || source.empty()) return std::nullopt;
        auto resolvedKey = RasterImageKey(baseDirectory, source);
        if (!resolvedKey) return std::nullopt;
        auto key = std::move(*resolvedKey);
        std::vector<std::uint8_t> encoded;
        auto remote = source.starts_with("https://") || source.starts_with("http://");
        auto data = source.starts_with("data:image/");
        std::filesystem::path path;
        if (remote)
        {
            if (auto found = rasterImages.find(key); found != rasterImages.end()) return found->second;
            if (rasterImageFailures.contains(key)) return std::nullopt;
            {
                std::scoped_lock lock(remoteState->mutex);
                auto found = remoteState->data.find(std::string(source));
                if (found != remoteState->data.end()) encoded = found->second;
            }
            if (encoded.empty())
            {
                QueueRemoteImage(std::string(source));
                return std::nullopt;
            }
        }
        else if (data)
        {
            auto comma = source.find(',');
            if (comma == std::string_view::npos || source.substr(0, comma).find(";base64") == std::string_view::npos) return std::nullopt;
            if (auto found = rasterImages.find(key); found != rasterImages.end()) return found->second;
            if (rasterImageFailures.contains(key)) return std::nullopt;
            auto decoded = DecodeBase64(source.substr(comma + 1));
            if (!decoded || decoded->empty()) return std::nullopt;
            encoded = std::move(*decoded);
        }
        else
        {
            auto sourceText = winrt::to_hstring(std::string(source));
            path = std::filesystem::path(sourceText.c_str());
            if (path.has_root_directory() && !path.has_root_name() && !baseDirectory.empty()) path = std::filesystem::path(baseDirectory) / path.relative_path();
            if (path.is_relative())
            {
                if (baseDirectory.empty()) return std::nullopt;
                path = std::filesystem::path(baseDirectory) / path;
            }
            path = std::filesystem::path(key);
        }
        if (auto found = rasterImages.find(key); found != rasterImages.end()) return found->second;
        if (rasterImageFailures.contains(key)) return std::nullopt;
        auto fail = [&]() -> std::optional<RasterImage>
        {
            rasterImageFailures.insert(key);
            return std::nullopt;
        };

        std::shared_ptr<std::vector<std::uint8_t>> encodedBacking;
        std::shared_ptr<GifInitialFrame const> gifInitial;
        auto gifCandidate = !encoded.empty()
            ? HasGifMagic(encoded)
            : HasGifExtension(path) || ProbeGifDimensions(baseDirectory, source).has_value();
        if (gifCandidate)
        {
            gifDimensionMisses.erase(key);
            auto pending = pendingGifImages.find(key);
            if (pending == pendingGifImages.end())
            {
                if (!encoded.empty())
                    encodedBacking = std::make_shared<std::vector<std::uint8_t>>(std::move(encoded));
                auto state = remoteState;
                auto completion = [state]
                {
                    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
                    {
                        std::scoped_lock lock(state->mutex);
                        if (!state->active) return;
                        ++state->generation;
                        dispatcher = state->dispatcher;
                    }
                    if (dispatcher)
                    {
                        dispatcher.TryEnqueue([state]
                        {
                            std::function<void()> invalidate;
                            {
                                std::scoped_lock lock(state->mutex);
                                if (state->active) invalidate = state->invalidate;
                            }
                            if (invalidate) invalidate();
                        });
                    }
                };
                auto decode = QueueGifInitialDecode(
                    path,
                    encodedBacking,
                    std::move(completion),
                    64u * 1024u * 1024u);
                pendingGifImages.emplace(key, PendingGifImage{ std::move(decode), std::move(encodedBacking), path });
                return std::nullopt;
            }
            if (!pending->second.decode->Complete()) return std::nullopt;
            if (pending->second.decode->Failed())
            {
                pendingGifImages.erase(pending);
                return fail();
            }
            gifInitial = pending->second.decode->Result();
            encodedBacking = pending->second.encodedBacking;
            path = pending->second.path;
            pendingGifImages.erase(pending);
        }

        ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        ::Microsoft::WRL::ComPtr<IWICStream> stream;
        if (!encodedBacking && !encoded.empty())
            encodedBacking = std::make_shared<std::vector<std::uint8_t>>(std::move(encoded));
        if (encodedBacking && !encodedBacking->empty())
        {
            if (encodedBacking->size() > (std::numeric_limits<DWORD>::max)()) return fail();
            if (FAILED(resources.wicFactory->CreateStream(stream.GetAddressOf()))
                || FAILED(stream->InitializeFromMemory(encodedBacking->data(), static_cast<DWORD>(encodedBacking->size())))) return fail();
            if (FAILED(resources.wicFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return fail();
        }
        else if (FAILED(resources.wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return fail();
        RasterImage image;
        if (auto decoded = DecodeGifAnimation(
                resources.wicFactory.Get(),
                resources.d2dContext.Get(),
                decoder.Get(),
                encodedBacking,
                gifInitial,
                path,
                [state = remoteState]
                {
                    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
                    {
                        std::scoped_lock lock(state->mutex);
                        if (!state->active) return;
                        dispatcher = state->dispatcher;
                    }
                    if (dispatcher)
                    {
                        dispatcher.TryEnqueue([state]
                        {
                            std::function<void()> invalidate;
                            {
                                std::scoped_lock lock(state->mutex);
                                if (state->active) invalidate = state->invalidate;
                            }
                            if (invalidate) invalidate();
                        });
                    }
                },
                64u * 1024u * 1024u))
        {
            image.bitmap = decoded->Bitmap();
            image.animation = std::move(decoded);
            image.width = static_cast<float>(image.animation->Width());
            image.height = static_cast<float>(image.animation->Height());
            image.bytes = image.animation->MemoryCost();
        }
        else
        {
            ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            UINT width = 0;
            UINT height = 0;
            if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || !frame
                || FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return fail();
            auto pixels = static_cast<std::uint64_t>(width) * height;
            if (pixels > 16ull * 1024ull * 1024ull) return fail();
            ::Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            if (FAILED(resources.wicFactory->CreateFormatConverter(converter.GetAddressOf()))
                || FAILED(converter->Initialize(
                    frame.Get(),
                    GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0.0,
                    WICBitmapPaletteTypeMedianCut))
                || FAILED(resources.d2dContext->CreateBitmapFromWicBitmap(
                    converter.Get(),
                    nullptr,
                    image.bitmap.GetAddressOf())) || !image.bitmap) return fail();
            image.width = static_cast<float>(width);
            image.height = static_cast<float>(height);
            image.bytes = static_cast<std::size_t>(pixels * 4);
        }

        while (!rasterImageOrder.empty() && (rasterImages.size() >= 32 || rasterImageBytes + image.bytes > 64 * 1024 * 1024))
        {
            auto oldest = std::move(rasterImageOrder.front());
            rasterImageOrder.pop_front();
            auto found = rasterImages.find(oldest);
            if (found == rasterImages.end()) continue;
            rasterImageBytes -= found->second.bytes;
            rasterImages.erase(found);
        }
        if (image.bytes <= 64 * 1024 * 1024)
        {
            rasterImageBytes += image.bytes;
            rasterImageOrder.push_back(key);
            rasterImages.emplace(key, image);
        }
        if (gifCandidate)
        {
            if (!gifDimensions.contains(key))
            {
                constexpr std::size_t dimensionLimit = 1024;
                while (gifDimensionOrder.size() >= dimensionLimit)
                {
                    gifDimensions.erase(gifDimensionOrder.front());
                    gifDimensionOrder.pop_front();
                }
                gifDimensionOrder.push_back(key);
            }
            gifDimensions.insert_or_assign(key, ImageDimensions{image.width, image.height});
        }
        return image;
    }

    void EditorRenderCache::ReleaseGifImage(
        std::wstring const& baseDirectory,
        std::string_view source)
    {
        auto key = RasterImageKey(baseDirectory, source);
        if (!key) return;
        if (auto pending = pendingGifImages.find(*key); pending != pendingGifImages.end())
        {
            pending->second.decode->Cancel();
            pendingGifImages.erase(pending);
        }
        auto found = rasterImages.find(*key);
        if (found == rasterImages.end() || !found->second.animation) return;
        // The cache owns one reference. Other prepared blocks keep the shared
        // animation alive and prevent a duplicate decoder from being created.
        if (found->second.animation.use_count() != 1) return;
        rasterImageBytes -= found->second.bytes;
        rasterImages.erase(found);
        std::erase(rasterImageOrder, *key);
    }
}


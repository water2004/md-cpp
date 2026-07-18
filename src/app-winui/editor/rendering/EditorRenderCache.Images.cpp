#include "pch.h"
#include "editor/rendering/EditorRenderCache.h"
#include "media/EditorGifDecoder.h"

import folia.core.image_metadata;
import folia.core.render_model;
import folia.core.types;

#include "editor/rendering/EditorContentPreparation.h"

namespace
{
    using ImageDimensions = winrt::Folia::EditorRenderCache::ImageDimensions;

    constexpr std::size_t MaximumEncodedImageBytes = 16u * 1024u * 1024u;
    constexpr std::size_t RemoteMetadataBytes = 64u * 1024u;

    std::optional<ImageDimensions> ValidDimensions(std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0
            || static_cast<std::uint64_t>(width) * height > 16ull * 1024ull * 1024ull)
            return std::nullopt;
        return ImageDimensions{static_cast<float>(width), static_cast<float>(height)};
    }

    bool AsciiEqualsIgnoreCase(char left, char right)
    {
        if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
        return left == right;
    }

    bool AsciiEndsWithIgnoreCase(std::string_view value, std::string_view suffix)
    {
        if (value.size() < suffix.size()) return false;
        return std::ranges::equal(
            value.substr(value.size() - suffix.size()),
            suffix,
            AsciiEqualsIgnoreCase);
    }

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

    bool HasGifMagic(std::filesystem::path const& path)
    {
        std::array<std::uint8_t, 6> header{};
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (stream.gcount() != static_cast<std::streamsize>(header.size())) return false;
        auto signature = std::string_view(reinterpret_cast<char const*>(header.data()), header.size());
        return signature == "GIF87a" || signature == "GIF89a";
    }

    std::optional<ImageDimensions> ImageDimensionsFromHeader(
        std::uint8_t const* bytes,
        std::size_t size)
    {
        if (!bytes) return std::nullopt;
        auto metadata = folia::probe_encoded_image_metadata(std::span(bytes, size));
        if (!metadata) return std::nullopt;
        return ImageDimensions{
            static_cast<float>(metadata->width),
            static_cast<float>(metadata->height),
        };
    }

    std::optional<std::vector<std::uint8_t>> DecodeImageDataUri(std::string_view source)
    {
        if (!source.starts_with("data:image/")) return std::nullopt;
        auto comma = source.find(',');
        if (comma == std::string_view::npos) return std::nullopt;
        auto metadata = source.substr(0, comma);
        if (metadata.find(";base64") != std::string_view::npos)
            return winrt::Folia::DecodeBase64(source.substr(comma + 1));
        std::vector<std::uint8_t> decoded;
        decoded.reserve((std::min)(source.size() - comma - 1, MaximumEncodedImageBytes));
        auto payload = source.substr(comma + 1);
        auto hex = [](char value) -> int
        {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        for (std::size_t index = 0; index < payload.size(); ++index)
        {
            if (decoded.size() >= MaximumEncodedImageBytes) return std::nullopt;
            if (payload[index] == '%' && index + 2 < payload.size())
            {
                auto high = hex(payload[index + 1]);
                auto low = hex(payload[index + 2]);
                if (high < 0 || low < 0) return std::nullopt;
                decoded.push_back(static_cast<std::uint8_t>((high << 4) | low));
                index += 2;
            }
            else
            {
                decoded.push_back(static_cast<std::uint8_t>(payload[index]));
            }
        }
        return decoded;
    }

    bool LooksLikeSvg(std::span<std::uint8_t const> bytes)
    {
        auto limit = (std::min)(bytes.size(), std::size_t{8192});
        auto text = std::string_view(reinterpret_cast<char const*>(bytes.data()), limit);
        if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'
            || text.front() == '\r' || text.front() == '\n')) text.remove_prefix(1);
        if (text.empty() || text.front() != '<' || text.find('\0') != std::string_view::npos) return false;
        for (std::size_t index = 0; index + 4 <= text.size(); ++index)
        {
            if (text[index] == '<'
                && AsciiEqualsIgnoreCase(text[index + 1], 's')
                && AsciiEqualsIgnoreCase(text[index + 2], 'v')
                && AsciiEqualsIgnoreCase(text[index + 3], 'g'))
            {
                auto boundary = index + 4 == text.size() ? ' ' : text[index + 4];
                return boundary == ' ' || boundary == '\t' || boundary == '\r'
                    || boundary == '\n' || boundary == '>' || boundary == '/';
            }
        }
        return false;
    }

    bool HasSvgHint(std::string_view source)
    {
        if (source.starts_with("data:image/svg+xml")) return true;
        auto end = source.find_first_of("?#");
        auto path = source.substr(0, end);
        return AsciiEndsWithIgnoreCase(path, ".svg");
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

    std::optional<ImageDimensions> ProbeWicDimensions(
        IWICImagingFactory* factory,
        std::filesystem::path const& path)
    {
        if (!factory || path.empty()) return std::nullopt;
        ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(
                path.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf()))) return std::nullopt;
        ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        UINT width = 0;
        UINT height = 0;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || !frame
            || FAILED(frame->GetSize(&width, &height))) return std::nullopt;
        return ValidDimensions(width, height);
    }

    std::optional<ImageDimensions> ProbeWicDimensions(
        IWICImagingFactory* factory,
        std::vector<std::uint8_t> const& bytes)
    {
        if (!factory || bytes.empty() || bytes.size() > (std::numeric_limits<DWORD>::max)())
            return std::nullopt;
        ::Microsoft::WRL::ComPtr<IWICStream> stream;
        ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateStream(stream.GetAddressOf()))
            || FAILED(stream->InitializeFromMemory(
                const_cast<std::uint8_t*>(bytes.data()),
                static_cast<DWORD>(bytes.size())))
            || FAILED(factory->CreateDecoderFromStream(
                stream.Get(),
                nullptr,
                WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf()))) return std::nullopt;
        ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        UINT width = 0;
        UINT height = 0;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || !frame
            || FAILED(frame->GetSize(&width, &height))) return std::nullopt;
        return ValidDimensions(width, height);
    }
}
namespace winrt::Folia
{
    std::optional<EditorRenderCache::ImageDimensions> EditorRenderCache::ProbeImageDimensions(
        EditorRenderResources const& resources,
        std::wstring const& baseDirectory,
        std::string_view source)
    {
        auto key = RasterImageKey(baseDirectory, source);
        if (!key) return std::nullopt;
        if (auto found = imageDimensions.find(*key); found != imageDimensions.end()) return found->second;
        if (imageDimensionMisses.contains(*key)) return std::nullopt;
        if (auto found = rasterImages.find(*key); found != rasterImages.end())
            return ImageDimensions{found->second.width, found->second.height};

        std::optional<ImageDimensions> dimensions;
        if (source.starts_with("data:image/"))
        {
            if (auto bytes = DecodeImageDataUri(source))
            {
                dimensions = ImageDimensionsFromHeader(bytes->data(), bytes->size());
                if (!dimensions) dimensions = ProbeWicDimensions(resources.wicFactory.Get(), *bytes);
            }
        }
        else if (source.starts_with("https://") || source.starts_with("http://"))
        {
            auto remoteSource = std::string(source);
            {
                std::scoped_lock lock(remoteState->mutex);
                if (auto found = remoteState->dimensions.find(remoteSource); found != remoteState->dimensions.end())
                    dimensions = found->second;
                else if (auto dataFound = remoteState->data.find(remoteSource); dataFound != remoteState->data.end())
                {
                    dimensions = ImageDimensionsFromHeader(dataFound->second.data(), dataFound->second.size());
                    if (!dimensions)
                        dimensions = ProbeWicDimensions(resources.wicFactory.Get(), dataFound->second);
                }
            }
            if (!dimensions) QueueRemoteImageDimensions(std::move(remoteSource));
        }
        else
        {
            dimensions = ProbeWicDimensions(resources.wicFactory.Get(), std::filesystem::path(*key));
        }
        if (!dimensions)
        {
            // Remote metadata is asynchronous; unlike local/data misses it
            // must remain probeable after the range request completes.
            if (!source.starts_with("https://") && !source.starts_with("http://"))
            {
                if (imageDimensionMisses.size() >= 2048) imageDimensionMisses.clear();
                imageDimensionMisses.insert(*key);
            }
            return std::nullopt;
        }
        constexpr std::size_t dimensionLimit = 1024;
        while (imageDimensionOrder.size() >= dimensionLimit)
        {
            imageDimensions.erase(imageDimensionOrder.front());
            imageDimensionOrder.pop_front();
        }
        imageDimensionOrder.push_back(*key);
        imageDimensions.emplace(*key, *dimensions);
        return dimensions;
    }

    EditorRenderCache::SvgSource EditorRenderCache::LoadSvgSource(
        std::wstring const& baseDirectory,
        std::string_view source,
        bool loadContent)
    {
        SvgSource result;
        result.candidate = HasSvgHint(source);
        if (!loadContent) return result;
        auto key = RasterImageKey(baseDirectory, source);
        if (!key) return result;

        std::vector<std::uint8_t> bytes;
        if (source.starts_with("data:image/"))
        {
            auto decoded = DecodeImageDataUri(source);
            if (!decoded) return result;
            bytes = std::move(*decoded);
        }
        else if (source.starts_with("https://") || source.starts_with("http://"))
        {
            auto remoteSource = std::string(source);
            auto failed = false;
            {
                std::scoped_lock lock(remoteState->mutex);
                if (auto found = remoteState->data.find(remoteSource); found != remoteState->data.end())
                    bytes = found->second;
                failed = remoteState->failed.contains(remoteSource);
            }
            if (bytes.empty())
            {
                if (!failed)
                {
                    QueueRemoteImage(std::move(remoteSource));
                    result.pending = true;
                }
                return result;
            }
        }
        else
        {
            auto path = std::filesystem::path(*key);
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) return result;
            auto length = stream.tellg();
            if (length <= 0 || static_cast<std::uint64_t>(length) > MaximumEncodedImageBytes)
                return result;
            bytes.resize(static_cast<std::size_t>(length));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) return result;
        }

        auto svg = LooksLikeSvg(bytes);
        result.candidate = result.candidate || svg;
        if (result.candidate)
            result.source.emplace(reinterpret_cast<char const*>(bytes.data()), bytes.size());
        return result;
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

    void EditorRenderCache::QueueRemoteImageDimensions(std::string source)
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
            request.Headers().TryAppendWithoutValidation(L"Range", L"bytes=0-65535");
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
                            auto loadOperation = reader.LoadAsync(static_cast<UINT32>(RemoteMetadataBytes));
                            loadOperation.Completed([response, reader, publish](auto const& loadAsync, winrt::Windows::Foundation::AsyncStatus loadStatus) mutable
                            {
                                if (loadStatus != winrt::Windows::Foundation::AsyncStatus::Completed)
                                {
                                    publish(std::nullopt);
                                    return;
                                }
                                try
                                {
                                    auto count = (std::min)(
                                        static_cast<std::size_t>(loadAsync.GetResults()),
                                        RemoteMetadataBytes);
                                    std::vector<std::uint8_t> header(count);
                                    reader.ReadBytes(winrt::array_view<std::uint8_t>(header));
                                    publish(ImageDimensionsFromHeader(header.data(), header.size()));
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
            if (auto found = rasterImages.find(key); found != rasterImages.end()) return found->second;
            if (rasterImageFailures.contains(key)) return std::nullopt;
            auto decoded = DecodeImageDataUri(source);
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
            : HasGifExtension(path) || HasGifMagic(path);
        if (gifCandidate)
        {
            imageDimensionMisses.erase(key);
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
        imageDimensionMisses.erase(key);
        if (!imageDimensions.contains(key))
        {
            constexpr std::size_t dimensionLimit = 1024;
            while (imageDimensionOrder.size() >= dimensionLimit)
            {
                imageDimensions.erase(imageDimensionOrder.front());
                imageDimensionOrder.pop_front();
            }
            imageDimensionOrder.push_back(key);
        }
        imageDimensions.insert_or_assign(key, ImageDimensions{image.width, image.height});
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

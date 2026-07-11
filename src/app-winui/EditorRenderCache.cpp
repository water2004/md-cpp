#include "pch.h"
#include "EditorRenderCache.h"

import elmd.core.render_model;
import elmd.core.types;

#include "EditorContentPreparation.h"

namespace winrt::ElMd
{
    EditorRenderCache::~EditorRenderCache()
    {
        Detach();
    }

    void EditorRenderCache::Attach(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher, std::function<void()> invalidate)
    {
        std::scoped_lock lock(remoteState->mutex);
        remoteState->dispatcher = dispatcher;
        remoteState->invalidate = std::move(invalidate);
        remoteState->active = true;
    }

    void EditorRenderCache::Detach()
    {
        {
            std::scoped_lock lock(remoteState->mutex);
            remoteState->active = false;
            remoteState->invalidate = {};
            remoteState->dispatcher = nullptr;
            remoteState->data.clear();
            remoteState->pending.clear();
            remoteState->failed.clear();
            remoteState->order.clear();
            remoteState->bytes = 0;
            ++remoteState->generation;
        }
        ClearDeviceResources();
    }

    void EditorRenderCache::ClearTextLayouts()
    {
        textLayouts.clear();
        textLayoutOrder.clear();
        textLayoutBytes = 0;
    }

    void EditorRenderCache::ClearSvgDocuments()
    {
        svgDocuments.clear();
        svgDocumentOrder.clear();
        svgDocumentBytes = 0;
    }

    void EditorRenderCache::ClearDeviceResources()
    {
        ClearTextLayouts();
        ClearSvgDocuments();
        rasterImages.clear();
        rasterImageFailures.clear();
        rasterImageOrder.clear();
        rasterImageBytes = 0;
    }

    std::uint64_t EditorRenderCache::RemoteImageGeneration() const
    {
        return remoteState->generation.load();
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
        std::wstring key;
        std::vector<std::uint8_t> encoded;
        auto remote = source.starts_with("https://") || source.starts_with("http://");
        auto data = source.starts_with("data:image/");
        std::filesystem::path path;
        if (remote)
        {
            key = L"url:" + std::wstring(winrt::to_hstring(std::string(source)));
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
            key = L"data:" + std::to_wstring(source.size()) + L":" + std::to_wstring(std::hash<std::string_view>{}(source));
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
            std::error_code error;
            auto absolute = std::filesystem::weakly_canonical(path, error);
            if (error) absolute = path.lexically_normal();
            key = absolute.wstring();
            path = std::move(absolute);
        }
        if (auto found = rasterImages.find(key); found != rasterImages.end()) return found->second;
        if (rasterImageFailures.contains(key)) return std::nullopt;
        auto fail = [&]() -> std::optional<RasterImage>
        {
            rasterImageFailures.insert(key);
            return std::nullopt;
        };

        ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        ::Microsoft::WRL::ComPtr<IWICStream> stream;
        if (!encoded.empty())
        {
            if (encoded.size() > (std::numeric_limits<DWORD>::max)()) return fail();
            if (FAILED(resources.wicFactory->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size())))) return fail();
            if (FAILED(resources.wicFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return fail();
        }
        else if (FAILED(resources.wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return fail();
        ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return fail();
        UINT width = 0;
        UINT height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return fail();
        auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (pixels > 16ull * 1024ull * 1024ull) return fail();
        ::Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (FAILED(resources.wicFactory->CreateFormatConverter(converter.GetAddressOf()))) return fail();
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) return fail();
        RasterImage image;
        if (FAILED(resources.d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, image.bitmap.GetAddressOf())) || !image.bitmap) return fail();
        image.width = static_cast<float>(width);
        image.height = static_cast<float>(height);
        image.bytes = static_cast<std::size_t>(pixels * 4);
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
        return image;
    }

    ::Microsoft::WRL::ComPtr<IDWriteTextLayout> EditorRenderCache::FindTextLayout(std::uint64_t key)
    {
        auto found = textLayouts.find(key);
        if (found == textLayouts.end()) return {};
        if (auto order = std::find(textLayoutOrder.begin(), textLayoutOrder.end(), key); order != textLayoutOrder.end())
        {
            textLayoutOrder.erase(order);
            textLayoutOrder.push_back(key);
        }
        return found->second.layout;
    }

    void EditorRenderCache::StoreTextLayout(std::uint64_t key, ::Microsoft::WRL::ComPtr<IDWriteTextLayout> const& layout, std::size_t bytes)
    {
        if (!layout) return;
        constexpr std::size_t budget = 16 * 1024 * 1024;
        constexpr std::size_t limit = 512;
        while (!textLayoutOrder.empty() && (textLayoutBytes + bytes > budget || textLayouts.size() >= limit))
        {
            auto oldest = textLayoutOrder.front();
            textLayoutOrder.pop_front();
            auto found = textLayouts.find(oldest);
            if (found == textLayouts.end()) continue;
            textLayoutBytes -= found->second.bytes;
            textLayouts.erase(found);
        }
        if (bytes > budget) return;
        textLayoutBytes += bytes;
        textLayoutOrder.push_back(key);
        textLayouts.emplace(key, CachedTextLayout{ layout, bytes });
    }

    ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> EditorRenderCache::FindOrCreateSvgDocument(ID2D1DeviceContext5* context, std::uint64_t renderId, std::string const& source, float width, float height)
    {
        if (!context || renderId == 0 || source.empty() || width <= 0.0f || height <= 0.0f) return {};
        if (auto found = svgDocuments.find(renderId); found != svgDocuments.end())
        {
            if (auto order = std::find(svgDocumentOrder.begin(), svgDocumentOrder.end(), renderId); order != svgDocumentOrder.end())
            {
                svgDocumentOrder.erase(order);
                svgDocumentOrder.push_back(renderId);
            }
            return found->second.document;
        }
        auto allocation = GlobalAlloc(GMEM_MOVEABLE, source.size());
        if (!allocation) return {};
        auto bytes = static_cast<char*>(GlobalLock(allocation));
        if (!bytes)
        {
            GlobalFree(allocation);
            return {};
        }
        std::memcpy(bytes, source.data(), source.size());
        GlobalUnlock(allocation);
        ::Microsoft::WRL::ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(allocation, TRUE, stream.GetAddressOf())) || !stream)
        {
            GlobalFree(allocation);
            return {};
        }
        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
        if (FAILED(context->CreateSvgDocument(stream.Get(), D2D1::SizeF(width, height), document.GetAddressOf())) || !document) return {};
        constexpr std::size_t budget = 24 * 1024 * 1024;
        constexpr std::size_t limit = 96;
        auto resourceCost = (std::max)(std::size_t{16 * 1024}, source.size() * 8);
        while (!svgDocumentOrder.empty() && (svgDocumentBytes + resourceCost > budget || svgDocuments.size() >= limit))
        {
            auto oldest = std::move(svgDocumentOrder.front());
            svgDocumentOrder.pop_front();
            auto found = svgDocuments.find(oldest);
            if (found == svgDocuments.end()) continue;
            svgDocumentBytes -= found->second.bytes;
            svgDocuments.erase(found);
        }
        if (resourceCost <= budget)
        {
            svgDocumentBytes += resourceCost;
            svgDocumentOrder.push_back(renderId);
            svgDocuments.emplace(renderId, CachedSvgDocument{ document, resourceCost });
        }
        return document;
    }
}

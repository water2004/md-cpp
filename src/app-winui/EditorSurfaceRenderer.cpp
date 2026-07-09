#include "pch.h"
#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"

import elmd.core.render_model;
import elmd.core.utf;

namespace winrt::ElMd
{
    constexpr float DocumentWidthDip = 900.0f;
    constexpr float DocumentHorizontalPaddingDip = 48.0f;
    constexpr float DocumentVerticalPaddingDip = 40.0f;
    constexpr float ParagraphFontSizeDip = 18.0f;
    constexpr float CodeFontSizeDip = 15.0f;
    constexpr float ParagraphLineHeightDip = 30.0f;
    constexpr float CodeLineHeightDip = 24.0f;

    std::wstring ToWide(std::u32string_view text)
    {
        std::wstring wide;
        wide.reserve(text.size());
        for (auto codepoint : text)
        {
            if (codepoint <= 0xFFFF)
            {
                wide.push_back(static_cast<wchar_t>(codepoint));
            }
            else
            {
                codepoint -= 0x10000;
                wide.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
                wide.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
            }
        }
        return wide;
    }

    std::u32string InlineText(elmd::InlineRenderItem const& item)
    {
        switch (item.kind)
        {
            case elmd::InlineRenderItem::Kind::Text:
            case elmd::InlineRenderItem::Kind::Marker:
                return item.text;
            case elmd::InlineRenderItem::Kind::Math:
                return U"$" + item.text + U"$";
            case elmd::InlineRenderItem::Kind::Image:
                return item.alt.empty() ? U"image" : elmd::utf8_to_cps(item.alt);
            case elmd::InlineRenderItem::Kind::Link: {
                std::u32string text;
                for (auto const& child : item.children)
                {
                    text += InlineText(child);
                }
                return text;
            }
        }
        return {};
    }

    std::u32string InlineText(std::vector<elmd::InlineRenderItem> const& items)
    {
        std::u32string text;
        for (auto const& item : items)
        {
            text += InlineText(item);
        }
        return text;
    }

    std::u32string TableText(elmd::RenderBlock const& block)
    {
        if (block.column_count == 0 || block.row_count == 0)
        {
            return U"Table";
        }

        std::u32string text;
        for (std::size_t row = 0; row < block.row_count; ++row)
        {
            text += U"| ";
            for (std::size_t column = 0; column < block.column_count; ++column)
            {
                auto index = row * block.column_count + column;
                if (index < block.table_cells.size())
                {
                    text += InlineText(block.table_cells[index]);
                }
                text += U" | ";
            }
            if (row == 0 && block.row_count > 1)
            {
                text += U"\n| ";
                for (std::size_t column = 0; column < block.column_count; ++column)
                {
                    text += U"--- | ";
                }
                text.push_back(U'\n');
            }
            else if (row + 1 < block.row_count)
            {
                text.push_back(U'\n');
            }
        }
        return text;
    }

    struct InlineStyleRange
    {
        UINT32 start = 0;
        UINT32 length = 0;
        elmd::InlineStyle style;
        bool marker = false;
    };

    std::u32string InlineText(std::vector<elmd::InlineRenderItem> const& items, std::vector<InlineStyleRange>& ranges)
    {
        std::u32string text;
        for (auto const& item : items)
        {
            auto start = static_cast<UINT32>(text.size());
            text += InlineText(item);
            auto length = static_cast<UINT32>(text.size()) - start;
            if (length > 0)
            {
                InlineStyleRange range;
                range.start = start;
                range.length = length;
                range.style = item.style;
                range.marker = item.kind == elmd::InlineRenderItem::Kind::Marker;
                ranges.push_back(range);
            }
        }
        return text;
    }

    std::size_t SourceStart(elmd::RenderBlock const& block)
    {
        return block.content_range.start.v;
    }

    std::size_t SourceEnd(elmd::RenderBlock const& block, std::u32string const& text)
    {
        auto sourceStart = SourceStart(block);
        return (std::min)(block.content_range.end.v, sourceStart + text.size());
    }

    float EditorSurfaceRenderer::CompositionScaleX(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleX());
    }

    float EditorSurfaceRenderer::CompositionScaleY(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleY());
    }

    void EditorSurfaceRenderer::ApplySwapChainTransform()
    {
        if (!swapChain)
        {
            return;
        }

        ::Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
        if (SUCCEEDED(swapChain.As(&swapChain2)))
        {
            DXGI_MATRIX_3X2_F matrix{};
            matrix._11 = 1.0f / surfaceScaleX;
            matrix._22 = 1.0f / surfaceScaleY;
            winrt::check_hresult(swapChain2->SetMatrixTransform(&matrix));
        }
    }

    void EditorSurfaceRenderer::ResetTargets()
    {
        renderTargetView = nullptr;
        d2dTarget = nullptr;
        textBrush = nullptr;
        mutedBrush = nullptr;
        accentBrush = nullptr;
        codeBrush = nullptr;
        panelBrush = nullptr;
        selectionBrush = nullptr;
        caretBrush = nullptr;
    }

    void EditorSurfaceRenderer::Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel)
    {
        if (swapChain)
        {
            return;
        }

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        winrt::check_hresult(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
            D3D11_SDK_VERSION,
            d3dDevice.GetAddressOf(),
            nullptr,
            d3dContext.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        winrt::check_hresult(d3dDevice.As(&dxgiDevice));

        D2D1_FACTORY_OPTIONS d2dOptions{};
#if defined(_DEBUG)
        d2dOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dOptions, d2dFactory.GetAddressOf()));
        winrt::check_hresult(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf()));
        winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.GetAddressOf()));

        winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())));
        winrt::check_hresult(dwriteFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            ParagraphFontSizeDip,
            L"en-us",
            textFormat.GetAddressOf()));
        winrt::check_hresult(textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        winrt::check_hresult(textFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, ParagraphLineHeightDip, ParagraphFontSizeDip * 1.2f));

        winrt::check_hresult(dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 38.0f, L"en-us", heading1Format.GetAddressOf()));
        winrt::check_hresult(dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"en-us", heading2Format.GetAddressOf()));
        winrt::check_hresult(dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"en-us", heading3Format.GetAddressOf()));
        winrt::check_hresult(dwriteFactory->CreateTextFormat(L"Cascadia Mono", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, CodeFontSizeDip, L"en-us", codeFormat.GetAddressOf()));
        for (auto format : { heading1Format.Get(), heading2Format.Get(), heading3Format.Get() })
        {
            winrt::check_hresult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        }
        winrt::check_hresult(codeFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        winrt::check_hresult(codeFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, CodeLineHeightDip, CodeFontSizeDip * 1.25f));

        ::Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetAdapter(adapter.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())));

        surfaceScaleX = CompositionScaleX(panel);
        surfaceScaleY = CompositionScaleY(panel);
        auto width = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualWidth() * surfaceScaleX)));
        auto height = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualHeight() * surfaceScaleY)));

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = false;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = 0;

        winrt::check_hresult(factory->CreateSwapChainForComposition(d3dDevice.Get(), &desc, nullptr, swapChain.GetAddressOf()));
        ApplySwapChainTransform();

        auto panelNative = panel.as<ISwapChainPanelNative>();
        winrt::check_hresult(panelNative->SetSwapChain(swapChain.Get()));

        surfaceWidth = width;
        surfaceHeight = height;
    }

    void EditorSurfaceRenderer::Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height)
    {
        if (!swapChain)
        {
            return;
        }

        auto newScaleX = CompositionScaleX(panel);
        auto newScaleY = CompositionScaleY(panel);
        auto newWidth = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(width * newScaleX)));
        auto newHeight = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(height * newScaleY)));
        if (newWidth == surfaceWidth && newHeight == surfaceHeight && newScaleX == surfaceScaleX && newScaleY == surfaceScaleY)
        {
            return;
        }

        ResetTargets();
        winrt::check_hresult(swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0));
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        surfaceScaleX = newScaleX;
        surfaceScaleY = newScaleY;
        ApplySwapChainTransform();
    }

    void EditorSurfaceRenderer::DrawDocument(detail::EditorSessionCore const& sessionCore)
    {
        visualBlocks.clear();
        auto documentLeft = DocumentHorizontalPaddingDip;
        auto documentTop = DocumentVerticalPaddingDip;
        auto documentRight = (std::min)(static_cast<float>(surfaceWidth) - DocumentHorizontalPaddingDip, documentLeft + DocumentWidthDip);
        auto y = documentTop - scrollOffset;
        auto selection = sessionCore.editor.selection().normalized_range();
        auto caret = sessionCore.editor.selection().active.v;

        auto createLayout = [&](std::wstring const& text, IDWriteTextFormat* format, float width)
        {
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            auto hr = dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, 100000.0f, layout.GetAddressOf());
            if (FAILED(hr) || !layout)
            {
                return ::Microsoft::WRL::ComPtr<IDWriteTextLayout>{};
            }
            return layout;
        };

        auto applyInlineStyles = [&](IDWriteTextLayout* layout, std::vector<InlineStyleRange> const& ranges)
        {
            if (!layout)
            {
                return;
            }

            for (auto const& range : ranges)
            {
                DWRITE_TEXT_RANGE textRange{ range.start, range.length };
                if (range.style.bold)
                {
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
                }
                if (range.style.italic)
                {
                    layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, textRange);
                }
                if (range.style.link)
                {
                    layout->SetUnderline(true, textRange);
                }
                if (range.style.code)
                {
                    layout->SetFontFamilyName(L"Cascadia Mono", textRange);
                    layout->SetFontSize(CodeFontSizeDip, textRange);
                }
                if (range.marker)
                {
                    layout->SetFontSize(ParagraphFontSizeDip * 0.82f, textRange);
                }
            }
        };

        auto measureTextHeight = [&](IDWriteTextLayout* layout, float fallbackHeight)
        {
            if (!layout)
            {
                return fallbackHeight;
            }

            DWRITE_TEXT_METRICS metrics{};
            if (FAILED(layout->GetMetrics(&metrics)))
            {
                return fallbackHeight;
            }

            return (std::max)(fallbackHeight, metrics.height);
        };

        if (sessionCore.renderModel.blocks.empty())
        {
            auto emptyText = winrt::hstring(L"Open a Markdown file or start editing to see the WYSIWYG surface.");
            auto rect = D2D1::RectF(documentLeft, y, documentRight, y + 80.0f);
            d2dContext->DrawTextW(emptyText.c_str(), static_cast<UINT32>(emptyText.size()), textFormat.Get(), rect, mutedBrush.Get());
            return;
        }

        for (auto const& block : sessionCore.renderModel.blocks)
        {
            IDWriteTextFormat* format = textFormat.Get();
            ID2D1Brush* brush = textBrush.Get();
            float height = 48.0f;
            float inset = 0.0f;
            float textTop = 4.0f;
            bool fillPanel = false;
            bool measureHeight = true;
            std::u32string text;
            std::vector<InlineStyleRange> inlineRanges;

            switch (block.kind)
            {
                case elmd::RenderBlockKind::Text:
                    text = InlineText(block.inline_items, inlineRanges);
                    if (block.block_style.margin_top >= 24.0f)
                    {
                        format = heading1Format.Get();
                        height = 58.0f;
                    }
                    else if (block.block_style.margin_top >= 20.0f)
                    {
                        format = heading2Format.Get();
                        height = 50.0f;
                    }
                    else if (block.block_style.margin_top >= 16.0f)
                    {
                        format = heading3Format.Get();
                    }
                    break;
                case elmd::RenderBlockKind::Code:
                    text = block.code_text;
                    format = codeFormat.Get();
                    brush = codeBrush.Get();
                    height = 64.0f;
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                case elmd::RenderBlockKind::Math:
                    text = U"$$\n" + block.tex + U"\n$$";
                    format = codeFormat.Get();
                    brush = accentBrush.Get();
                    height = 64.0f;
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                case elmd::RenderBlockKind::Table:
                    text = TableText(block);
                    format = codeFormat.Get();
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                case elmd::RenderBlockKind::Image:
                    text = U"Image: " + (block.alt.empty() ? elmd::utf8_to_cps(block.src) : elmd::utf8_to_cps(block.alt)) + U"\n" + elmd::utf8_to_cps(block.src);
                    brush = mutedBrush.Get();
                    height = 72.0f;
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                case elmd::RenderBlockKind::Unsupported:
                    text = elmd::utf8_to_cps(block.raw);
                    brush = mutedBrush.Get();
                    height = 64.0f;
                    break;
                default:
                    text = InlineText(block.inline_items, inlineRanges);
                    brush = mutedBrush.Get();
                    break;
            }

            auto wide = ToWide(text);
            auto textWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
            auto layout = createLayout(wide, format, textWidth);
            applyInlineStyles(layout.Get(), inlineRanges);
            if (measureHeight)
            {
                auto fallbackHeight = format == codeFormat.Get() ? CodeLineHeightDip : ParagraphLineHeightDip;
                auto bottomPadding = fillPanel ? 16.0f : 8.0f;
                height = textTop + measureTextHeight(layout.Get(), fallbackHeight) + bottomPadding;
            }
            if (fillPanel)
            {
                d2dContext->FillRectangle(D2D1::RectF(documentLeft, y, documentRight, y + height), panelBrush.Get());
            }
            auto origin = D2D1::Point2F(documentLeft + inset, y + textTop);
            if (layout)
            {
                auto sourceStart = SourceStart(block);
                auto sourceEnd = SourceEnd(block, text);
                if (!selection.is_empty() && selection.end.v > sourceStart && selection.start.v < sourceEnd)
                {
                    auto rangeStart = (std::max)(selection.start.v, sourceStart) - sourceStart;
                    auto rangeEnd = (std::min)(selection.end.v, sourceEnd) - sourceStart;
                    UINT32 actualCount = 0;
                    auto hr = layout->HitTestTextRange(static_cast<UINT32>(rangeStart), static_cast<UINT32>(rangeEnd - rangeStart), origin.x, origin.y, nullptr, 0, &actualCount);
                    if (hr == E_NOT_SUFFICIENT_BUFFER && actualCount > 0)
                    {
                        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                        if (SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(rangeStart), static_cast<UINT32>(rangeEnd - rangeStart), origin.x, origin.y, metrics.data(), actualCount, &actualCount)))
                        {
                            for (UINT32 i = 0; i < actualCount; ++i)
                            {
                                auto const& metric = metrics[i];
                                d2dContext->FillRectangle(D2D1::RectF(metric.left, metric.top, metric.left + metric.width, metric.top + metric.height), selectionBrush.Get());
                            }
                        }
                    }
                }

                d2dContext->DrawTextLayout(origin, layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

                if (sourceStart <= caret && caret <= sourceEnd)
                {
                    FLOAT caretX = 0.0f;
                    FLOAT caretY = 0.0f;
                    DWRITE_HIT_TEST_METRICS caretMetrics{};
                    if (SUCCEEDED(layout->HitTestTextPosition(static_cast<UINT32>(caret - sourceStart), false, &caretX, &caretY, &caretMetrics)))
                    {
                        auto x = origin.x + caretX;
                        auto top = origin.y + caretY;
                        d2dContext->DrawLine(D2D1::Point2F(x, top), D2D1::Point2F(x, top + caretMetrics.height), caretBrush.Get(), 1.5f);
                    }
                }

                VisualBlock visualBlock;
                visualBlock.rect = D2D1::RectF(documentLeft, y, documentRight, y + height);
                visualBlock.textOrigin = origin;
                visualBlock.textWidth = textWidth;
                visualBlock.sourceStart = sourceStart;
                visualBlock.sourceEnd = sourceEnd;
                visualBlock.documentY = y + scrollOffset;
                visualBlock.text = std::move(text);
                visualBlock.layout = layout;
                visualBlocks.push_back(std::move(visualBlock));
            }
            y += height + 8.0f;
        }

        totalDocumentHeight = y + scrollOffset + DocumentVerticalPaddingDip;
    }

    void EditorSurfaceRenderer::ScrollBy(float delta)
    {
        auto maxScroll = (std::max)(0.0f, totalDocumentHeight - static_cast<float>(surfaceHeight));
        scrollOffset = (std::min)(maxScroll, (std::max)(0.0f, scrollOffset + delta));
    }

    void EditorSurfaceRenderer::ScrollToSourceOffset(std::size_t sourceOffset)
    {
        for (auto const& block : visualBlocks)
        {
            if (block.sourceStart <= sourceOffset && sourceOffset <= block.sourceEnd)
            {
                auto maxScroll = (std::max)(0.0f, totalDocumentHeight - static_cast<float>(surfaceHeight));
                scrollOffset = (std::min)(maxScroll, (std::max)(0.0f, block.documentY - DocumentVerticalPaddingDip));
                return;
            }
        }
    }

    std::optional<std::size_t> EditorSurfaceRenderer::HitTest(float x, float y) const
    {
        x *= surfaceScaleX;
        y *= surfaceScaleY;

        for (auto const& block : visualBlocks)
        {
            if (x < block.rect.left || x > block.rect.right || y < block.rect.top || y > block.rect.bottom || !block.layout)
            {
                continue;
            }

            BOOL isTrailingHit = false;
            BOOL isInside = false;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(block.layout->HitTestPoint(x - block.textOrigin.x, y - block.textOrigin.y, &isTrailingHit, &isInside, &metrics)))
            {
                auto position = static_cast<std::size_t>(metrics.textPosition + (isTrailingHit ? metrics.length : 0));
                return (std::min)(block.sourceEnd, block.sourceStart + position);
            }
        }

        if (!visualBlocks.empty())
        {
            if (y < visualBlocks.front().rect.top)
            {
                return visualBlocks.front().sourceStart;
            }
            return visualBlocks.back().sourceEnd;
        }

        return std::nullopt;
    }

    void EditorSurfaceRenderer::Render(detail::EditorSessionCore const& sessionCore)
    {
        if (!swapChain || !d3dDevice || !d3dContext)
        {
            return;
        }

        if (!renderTargetView)
        {
            ::Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            winrt::check_hresult(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf())));
            winrt::check_hresult(d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView.GetAddressOf()));
        }

        if (!d2dTarget)
        {
            ::Microsoft::WRL::ComPtr<IDXGISurface> surface;
            winrt::check_hresult(swapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())));

            D2D1_BITMAP_PROPERTIES1 properties{};
            properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            properties.dpiX = 96.0f;
            properties.dpiY = 96.0f;
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

            winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties, d2dTarget.GetAddressOf()));
            d2dContext->SetTarget(d2dTarget.Get());
        }

        if (!textBrush)
        {
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.90f, 0.96f, 1.0f), textBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.58f, 0.65f, 0.75f, 1.0f), mutedBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.43f, 0.72f, 1.0f, 1.0f), accentBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.80f, 0.86f, 0.92f, 1.0f), codeBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.105f, 0.130f, 0.165f, 1.0f), panelBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.42f, 0.70f, 0.55f), selectionBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.96f, 1.0f, 1.0f), caretBrush.GetAddressOf()));
        }

        d2dContext->BeginDraw();
        d2dContext->Clear(D2D1::ColorF(0.070f, 0.086f, 0.110f, 1.0f));
        DrawDocument(sessionCore);
        winrt::check_hresult(d2dContext->EndDraw());

        winrt::check_hresult(swapChain->Present(1, 0));
    }
}

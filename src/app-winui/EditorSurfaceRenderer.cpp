#include "pch.h"
#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"

import elmd.core.render_model;
import elmd.core.table_edit;
import elmd.core.utf;

namespace winrt::ElMd
{
    D2D1_COLOR_F Rgba(float red, float green, float blue, float alpha = 1.0f)
    {
        return D2D1::ColorF(red, green, blue, alpha);
    }

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

    std::string SvgColor(D2D1_COLOR_F color)
    {
        auto component = [](float value)
        {
            return static_cast<unsigned>((std::clamp)(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        char result[8]{};
        std::snprintf(result, sizeof(result), "#%02X%02X%02X", component(color.r), component(color.g), component(color.b));
        return result;
    }

    std::string RecolorMathSvg(std::string svg, D2D1_COLOR_F color)
    {
        auto replacement = SvgColor(color);
        constexpr std::string_view needle = "currentColor";
        std::size_t offset = 0;
        while ((offset = svg.find(needle, offset)) != std::string::npos)
        {
            svg.replace(offset, needle.size(), replacement);
            offset += replacement.size();
        }
        return svg;
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

    struct InlineStyleRange
    {
        UINT32 start = 0;
        UINT32 length = 0;
        elmd::InlineStyle style;
        bool marker = false;
    };

    struct DisplayInlineText
    {
        struct MathOverlay
        {
            std::uint32_t displayStart = 0;
            MathJaxSvg svg;
        };

        std::u32string text;
        std::vector<std::size_t> displayToSource;
        std::vector<InlineStyleRange> ranges;
        std::vector<MathOverlay> mathOverlays;
    };

    bool IsStyleMarker(elmd::InlineRenderItem const& item)
    {
        if (item.kind != elmd::InlineRenderItem::Kind::Marker) return false;
        bool backticks = !item.text.empty();
        for (char32_t ch : item.text) if (ch != U'`') backticks = false;
        return item.text == U"*" || item.text == U"**" || item.text == U"~~" || backticks;
    }

    bool IsHeadingMarker(elmd::InlineRenderItem const& item)
    {
        if (item.kind != elmd::InlineRenderItem::Kind::Marker || item.text.size() < 2 || item.text.back() != U' ')
        {
            return false;
        }
        for (std::size_t index = 0; index + 1 < item.text.size(); ++index)
        {
            if (item.text[index] != U'#')
            {
                return false;
            }
        }
        return true;
    }

    bool CaretTouchesRange(std::size_t caret, elmd::CharRange const& range)
    {
        auto start = range.start.v;
        auto end = range.end.v;
        if (start <= caret && caret <= end)
        {
            return true;
        }
        return (start > 0 && caret == start - 1) || (end < (std::numeric_limits<std::size_t>::max)() && caret == end + 1);
    }

    std::vector<bool> RevealedStyleMarkers(std::vector<elmd::InlineRenderItem> const& items, std::size_t caret)
    {
        std::vector<bool> visible(items.size(), true);
        std::vector<std::pair<std::u32string, std::size_t>> stack;
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            auto const& item = items[index];
            if (!IsStyleMarker(item))
            {
                continue;
            }

            auto open = std::find_if(stack.rbegin(), stack.rend(), [&](auto const& entry)
            {
                return entry.first == item.text;
            });
            if (open == stack.rend())
            {
                stack.push_back({ item.text, index });
                visible[index] = false;
                continue;
            }

            auto openIndex = open->second;
            auto reveal = items[openIndex].source_range.start.v <= caret && caret <= item.source_range.end.v;
            visible[openIndex] = reveal;
            visible[index] = reveal;
            stack.erase(std::next(open).base());
        }

        for (auto const& entry : stack)
        {
            visible[entry.second] = true;
        }
        return visible;
    }

    void AppendDisplayText(DisplayInlineText& display, std::u32string const& text, std::size_t sourceStart, elmd::InlineStyle style, bool marker)
    {
        auto start = static_cast<UINT32>(display.text.size());
        display.text += text;
        auto length = static_cast<UINT32>(display.text.size()) - start;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            display.displayToSource.push_back(sourceStart + index);
        }
        if (length > 0)
        {
            InlineStyleRange range;
            range.start = start;
            range.length = length;
            range.style = style;
            range.marker = marker;
            display.ranges.push_back(range);
        }
    }

    void AppendSourceText(DisplayInlineText& display, std::u32string const& sourceText, std::size_t start, std::size_t end, elmd::InlineStyle style, bool marker)
    {
        start = (std::min)(start, sourceText.size());
        end = (std::min)((std::max)(end, start), sourceText.size());
        AppendDisplayText(display, std::u32string(sourceText.begin() + start, sourceText.begin() + end), start, style, marker);
    }

    void AppendMathPlaceholder(DisplayInlineText& display, std::size_t count, std::size_t sourceOffset)
    {
        auto start = static_cast<UINT32>(display.text.size());
        display.text.append(count, U'\u2007');
        display.displayToSource.insert(display.displayToSource.end(), count, sourceOffset);
        if (count > 0)
        {
            display.ranges.push_back(InlineStyleRange{ start, static_cast<UINT32>(count), elmd::InlineStyle::plain(), false });
        }
    }

    DisplayInlineText BuildDisplayInlineText(
        std::vector<elmd::InlineRenderItem> const& items,
        std::size_t caret,
        std::size_t sourceEnd,
        std::u32string const& sourceText,
        MathJaxRenderer& mathJax,
        float fontSize,
        float containerWidth,
        bool svgSupported)
    {
        DisplayInlineText display;
        auto markerVisibility = RevealedStyleMarkers(items, caret);
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            auto const& item = items[index];
            if (IsStyleMarker(item) && !markerVisibility[index])
            {
                continue;
            }
            if (IsHeadingMarker(item) && !(item.source_range.start.v <= caret && caret <= sourceEnd))
            {
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Math)
            {
                if (!svgSupported)
                {
                    AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                    continue;
                }
                auto math = mathJax.Render(elmd::cps_to_utf8(item.text), false, fontSize, containerWidth);
                auto editing = CaretTouchesRange(caret, item.source_range);
                if (!math)
                {
                    AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                    continue;
                }

                if (editing)
                {
                    AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                }
                auto overlayStart = static_cast<UINT32>(display.text.size());
                auto placeholderCount = static_cast<std::size_t>((std::max)(1.0f, std::ceil(math.width / (fontSize * 0.55f)))) + 1;
                AppendMathPlaceholder(display, placeholderCount, item.source_range.end.v);
                display.mathOverlays.push_back(DisplayInlineText::MathOverlay{ overlayStart, std::move(math) });
                continue;
            }
            AppendDisplayText(display, InlineText(item), item.source_range.start.v, item.style, item.kind == elmd::InlineRenderItem::Kind::Marker);
        }
        display.displayToSource.push_back(sourceEnd);
        return display;
    }

    DisplayInlineText BuildCodeBlockText(elmd::RenderBlock const& block, std::size_t caret, std::u32string const& sourceText)
    {
        DisplayInlineText display;
        auto showFence = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
        if (showFence)
        {
            AppendSourceText(display, sourceText, block.source_range.start.v, block.content_range.start.v, elmd::InlineStyle::plain(), true);
        }
        std::size_t contentVisibleEnd = block.content_range.end.v;
        if (!showFence && contentVisibleEnd > block.content_range.start.v && contentVisibleEnd <= sourceText.size() && sourceText[contentVisibleEnd - 1] == U'\n') --contentVisibleEnd;
        AppendSourceText(display, sourceText, block.content_range.start.v, contentVisibleEnd, elmd::InlineStyle::plain(), false);
        std::size_t visibleEnd = contentVisibleEnd;
        if (showFence)
        {
            visibleEnd = block.source_range.end.v;
            if (visibleEnd > block.content_range.end.v && visibleEnd <= sourceText.size() && sourceText[visibleEnd - 1] == U'\n') --visibleEnd;
            AppendSourceText(display, sourceText, block.content_range.end.v, visibleEnd, elmd::InlineStyle::plain(), true);
        }
        display.displayToSource.push_back(visibleEnd);
        return display;
    }

    EditorSurfaceRenderer::EditorStyleSheet EditorSurfaceRenderer::CreateStyleSheet(Theme value)
    {
        EditorStyleSheet sheet;
        sheet.body = FontStyle{ L"Microsoft YaHei UI", 18.0f, 29.0f, DWRITE_FONT_WEIGHT_NORMAL };
        sheet.heading1 = FontStyle{ L"Microsoft YaHei UI", 38.0f, 46.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading2 = FontStyle{ L"Microsoft YaHei UI", 30.0f, 37.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading3 = FontStyle{ L"Microsoft YaHei UI", 24.0f, 30.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.code = FontStyle{ L"Cascadia Code", 15.0f, 24.0f, DWRITE_FONT_WEIGHT_NORMAL };

        if (value == Theme::Light)
        {
            sheet.canvasColor = Rgba(0.982f, 0.984f, 0.988f);
            sheet.textColor = Rgba(0.125f, 0.137f, 0.160f);
            sheet.mutedColor = Rgba(0.420f, 0.455f, 0.520f);
            sheet.accentColor = Rgba(0.145f, 0.388f, 0.922f);
            sheet.codeTextColor = Rgba(0.180f, 0.205f, 0.250f);
            sheet.panelColor = Rgba(0.940f, 0.945f, 0.955f);
            sheet.selectionColor = Rgba(0.370f, 0.570f, 0.960f, 0.30f);
            sheet.caretColor = Rgba(0.065f, 0.075f, 0.090f);
            return sheet;
        }

        sheet.canvasColor = Rgba(0.070f, 0.078f, 0.098f);
        sheet.textColor = Rgba(0.895f, 0.910f, 0.940f);
        sheet.mutedColor = Rgba(0.545f, 0.585f, 0.665f);
        sheet.accentColor = Rgba(0.480f, 0.635f, 0.970f);
        sheet.codeTextColor = Rgba(0.875f, 0.895f, 0.925f);
        sheet.panelColor = Rgba(0.100f, 0.113f, 0.140f);
        sheet.selectionColor = Rgba(0.255f, 0.390f, 0.700f, 0.44f);
        sheet.caretColor = Rgba(0.965f, 0.975f, 1.000f);
        return sheet;
    }

    std::size_t DisplayPositionForSource(std::vector<std::size_t> const& displayToSource, std::size_t sourceOffset)
    {
        if (displayToSource.empty())
        {
            return 0;
        }

        for (std::size_t index = 0; index < displayToSource.size(); ++index)
        {
            if (displayToSource[index] >= sourceOffset)
            {
                return index;
            }
        }
        return displayToSource.size() - 1;
    }

    std::size_t SourceStart(elmd::RenderBlock const& block)
    {
        return block.content_range.start.v;
    }

    std::size_t SourceEnd(elmd::RenderBlock const& block, std::u32string const& text)
    {
        (void)text;
        return block.content_range.end.v;
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
        ResetBrushes();
    }

    void EditorSurfaceRenderer::ResetBrushes()
    {
        textBrush = nullptr;
        mutedBrush = nullptr;
        accentBrush = nullptr;
        codeBrush = nullptr;
        panelBrush = nullptr;
        selectionBrush = nullptr;
        caretBrush = nullptr;
    }

    void EditorSurfaceRenderer::RebuildTextFormats()
    {
        if (!dwriteFactory)
        {
            return;
        }

        auto createFormat = [&](FontStyle const& font, ::Microsoft::WRL::ComPtr<IDWriteTextFormat>& target)
        {
            target = nullptr;
            winrt::check_hresult(dwriteFactory->CreateTextFormat(
                font.family.c_str(),
                nullptr,
                font.weight,
                font.style,
                DWRITE_FONT_STRETCH_NORMAL,
                font.size,
                L"en-us",
                target.GetAddressOf()));
            winrt::check_hresult(target->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
            winrt::check_hresult(target->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, font.lineHeight, font.size * 1.2f));
        };

        createFormat(styleSheet.body, textFormat);
        createFormat(styleSheet.heading1, heading1Format);
        createFormat(styleSheet.heading2, heading2Format);
        createFormat(styleSheet.heading3, heading3Format);
        createFormat(styleSheet.code, codeFormat);
    }

    void EditorSurfaceRenderer::SetTheme(Theme value)
    {
        if (theme == value)
        {
            return;
        }

        theme = value;
        styleSheet = CreateStyleSheet(value);
        RebuildTextFormats();
        ResetBrushes();
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
        RebuildTextFormats();

        ::Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetAdapter(adapter.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())));

        surfaceScaleX = CompositionScaleX(panel);
        surfaceScaleY = CompositionScaleY(panel);
        surfaceWidthDip = static_cast<float>((std::max)(1.0, panel.ActualWidth()));
        surfaceHeightDip = static_cast<float>((std::max)(1.0, panel.ActualHeight()));
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
        auto newWidthDip = static_cast<float>((std::max)(1.0, width));
        auto newHeightDip = static_cast<float>((std::max)(1.0, height));
        auto newWidth = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(width * newScaleX)));
        auto newHeight = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(height * newScaleY)));
        if (newWidth == surfaceWidth && newHeight == surfaceHeight && newWidthDip == surfaceWidthDip && newHeightDip == surfaceHeightDip && newScaleX == surfaceScaleX && newScaleY == surfaceScaleY)
        {
            return;
        }

        ResetTargets();
        winrt::check_hresult(swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0));
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        surfaceWidthDip = newWidthDip;
        surfaceHeightDip = newHeightDip;
        surfaceScaleX = newScaleX;
        surfaceScaleY = newScaleY;
        ApplySwapChainTransform();
    }

    void EditorSurfaceRenderer::DrawDocument(detail::EditorSessionCore const& sessionCore)
    {
        visualBlocks.clear();
        visualLines.clear();
        visualTables.clear();
        auto documentLeft = styleSheet.horizontalPadding;
        auto documentTop = styleSheet.verticalPadding;
        auto documentRight = (std::min)(surfaceWidthDip - styleSheet.horizontalPadding, documentLeft + styleSheet.documentWidth);
        auto y = documentTop - scrollOffset;
        auto selection = sessionCore.editor.selection().normalized_range();
        auto caret = sessionCore.editor.selection().active.v;
        auto sourceText = sessionCore.editor.text_cps();
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> svgContext;
        auto svgSupported = SUCCEEDED(d2dContext.As(&svgContext)) && svgContext;

        auto drawMathSvg = [&](MathJaxSvg const& math, D2D1_POINT_2F origin, D2D1_COLOR_F color)
        {
            if (!math || !svgSupported)
            {
                return;
            }

            auto svg = RecolorMathSvg(math.svg, color);
            auto allocation = GlobalAlloc(GMEM_MOVEABLE, svg.size());
            if (!allocation)
            {
                return;
            }
            auto bytes = static_cast<char*>(GlobalLock(allocation));
            if (!bytes)
            {
                GlobalFree(allocation);
                return;
            }
            std::memcpy(bytes, svg.data(), svg.size());
            GlobalUnlock(allocation);

            ::Microsoft::WRL::ComPtr<IStream> stream;
            if (FAILED(CreateStreamOnHGlobal(allocation, TRUE, stream.GetAddressOf())) || !stream)
            {
                GlobalFree(allocation);
                return;
            }
            ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
            if (FAILED(svgContext->CreateSvgDocument(stream.Get(), D2D1::SizeF(math.width, math.height), document.GetAddressOf())) || !document)
            {
                return;
            }
            D2D1_MATRIX_3X2_F transform{};
            svgContext->GetTransform(&transform);
            svgContext->SetTransform(D2D1::Matrix3x2F::Translation(origin.x, origin.y) * transform);
            svgContext->DrawSvgDocument(document.Get());
            svgContext->SetTransform(transform);
        };

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
                if (range.style.strikethrough)
                {
                    layout->SetStrikethrough(true, textRange);
                }
                if (range.style.code)
                {
                    layout->SetFontFamilyName(styleSheet.code.family.c_str(), textRange);
                    layout->SetFontSize(styleSheet.code.size, textRange);
                }
                if (range.marker && !range.style.heading_level)
                {
                    layout->SetFontSize(styleSheet.body.size * 0.82f, textRange);
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

        auto addVisualLinesForBlock = [&](std::size_t blockIndex)
        {
            auto const& block = visualBlocks[blockIndex];
            if (!block.layout || block.displayToSource.empty())
            {
                return;
            }

            UINT32 lineCount = 0;
            auto hr = block.layout->GetLineMetrics(nullptr, 0, &lineCount);
            if (hr != E_NOT_SUFFICIENT_BUFFER || lineCount == 0)
            {
                return;
            }

            std::vector<DWRITE_LINE_METRICS> metrics(lineCount);
            if (FAILED(block.layout->GetLineMetrics(metrics.data(), lineCount, &lineCount)))
            {
                return;
            }

            UINT32 textPosition = 0;
            float lineTop = block.textOrigin.y;
            UINT32 prevNewlineLength = 0;
            for (UINT32 lineIndex = 0; lineIndex < lineCount; ++lineIndex)
            {
                auto const& line = metrics[lineIndex];
                auto lineEndPosition = textPosition + line.length;
                auto visibleEndPosition = lineEndPosition >= line.newlineLength ? lineEndPosition - line.newlineLength : lineEndPosition;
                auto startIndex = (std::min)(static_cast<std::size_t>(textPosition), block.displayToSource.size() - 1);
                auto endIndex = (std::min)(static_cast<std::size_t>(visibleEndPosition), block.displayToSource.size() - 1);
                VisualLine visualLine;
                visualLine.blockIndex = blockIndex;
                visualLine.sourceStart = block.displayToSource[startIndex];
                visualLine.sourceEnd = block.displayToSource[endIndex];
                visualLine.displayStart = textPosition;
                visualLine.displayEnd = visibleEndPosition;
                visualLine.wrapContinuation = lineIndex > 0 && prevNewlineLength == 0;
                visualLine.rect = D2D1::RectF(block.textOrigin.x, lineTop, block.textOrigin.x + block.textWidth, lineTop + line.height);
                visualLines.push_back(visualLine);
                textPosition = lineEndPosition;
                lineTop += line.height;
                prevNewlineLength = line.newlineLength;
            }
        };

        if (sessionCore.renderModel.blocks.empty() && sourceText.empty())
        {
            auto emptyText = winrt::hstring(L"Open a Markdown file or start editing to see the WYSIWYG surface.");
            auto rect = D2D1::RectF(documentLeft, y, documentRight, y + 80.0f);
            d2dContext->DrawTextW(emptyText.c_str(), static_cast<UINT32>(emptyText.size()), textFormat.Get(), rect, mutedBrush.Get());
            return;
        }

        bool previousBlank = false;
        for (std::size_t blockIndex = 0; blockIndex < sessionCore.renderModel.blocks.size(); ++blockIndex)
        {
            auto const& block = sessionCore.renderModel.blocks[blockIndex];
            bool currentBlank = block.kind == elmd::RenderBlockKind::Blank;
            if (blockIndex > 0 && !(previousBlank && currentBlank))
            {
                if (previousBlank || currentBlank)
                {
                    y += styleSheet.blockGap * 0.5f;
                }
                else
                {
                    y += styleSheet.blockGap;
                }
            }
            if (block.kind == elmd::RenderBlockKind::Table)
            {
                auto tableSource = elmd::table_source_at(sourceText, block.source_range.start.v);
                if (tableSource && tableSource->rows.size() >= 2 && tableSource->column_count > 0)
                {
                    VisualTable table;
                    table.sourceStart = block.source_range.start.v;
                    table.sourceEnd = block.source_range.end.v;
                    table.columnCount = tableSource->column_count;
                    table.rowCount = tableSource->rows.size() - 1;
                    auto tableWidth = (std::max)(1.0f, documentRight - documentLeft);
                    auto columnWidth = tableWidth / static_cast<float>(table.columnCount);
                    std::vector<float> rowHeights(table.rowCount, styleSheet.body.lineHeight + 16.0f);

                    for (std::size_t row = 0; row < table.rowCount; ++row)
                    {
                        auto sourceRowIndex = row == 0 ? 0 : row + 1;
                        auto const& sourceRow = tableSource->rows[sourceRowIndex];
                        for (std::size_t column = 0; column < table.columnCount; ++column)
                        {
                            auto sourceStart = sourceRow.line_range.end.v;
                            auto sourceEnd = sourceStart;
                            std::u32string cellText;
                            if (column < sourceRow.cells.size())
                            {
                                auto const& sourceCell = sourceRow.cells[column];
                                sourceStart = sourceCell.content_range.start.v;
                                sourceEnd = sourceCell.content_range.end.v;
                                cellText = sourceCell.text;
                            }
                            auto displayText = cellText;
                            auto wide = ToWide(displayText);
                            auto layout = createLayout(wide, textFormat.Get(), (std::max)(1.0f, columnWidth - 20.0f));
                            auto cellTextHeight = styleSheet.body.lineHeight;
                            if (layout)
                            {
                                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                                if (row == 0)
                                {
                                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(wide.size())});
                                }
                                auto alignment = column < block.table_aligns.size() ? block.table_aligns[column] : elmd::TableAlignment::None;
                                if (alignment == elmd::TableAlignment::Center) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                                else if (alignment == elmd::TableAlignment::Right) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                                else layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                                DWRITE_TEXT_METRICS metrics{};
                                if (SUCCEEDED(layout->GetMetrics(&metrics)))
                                {
                                    cellTextHeight = metrics.height;
                                    rowHeights[row] = (std::max)(rowHeights[row], metrics.height + 16.0f);
                                }
                            }
                            VisualTableCell cell;
                            cell.sourceStart = sourceStart;
                            cell.sourceEnd = sourceEnd;
                            cell.row = row;
                            cell.column = column;
                            cell.text = std::move(displayText);
                            cell.textHeight = cellTextHeight;
                            cell.layout = std::move(layout);
                            for (std::size_t index = 0; index < cell.text.size(); ++index)
                            {
                                cell.displayToSource.push_back((std::min)(sourceStart + index, sourceEnd));
                            }
                            cell.displayToSource.push_back(sourceEnd);
                            table.cells.push_back(std::move(cell));
                        }
                    }

                    table.columnBoundaries.reserve(table.columnCount + 1);
                    for (std::size_t column = 0; column <= table.columnCount; ++column) table.columnBoundaries.push_back(documentLeft + columnWidth * static_cast<float>(column));
                    table.rowBoundaries.reserve(table.rowCount + 1);
                    table.rowBoundaries.push_back(y);
                    for (auto rowHeight : rowHeights) table.rowBoundaries.push_back(table.rowBoundaries.back() + rowHeight);
                    table.rect = D2D1::RectF(documentLeft, y, documentRight, table.rowBoundaries.back());

                    for (auto& cell : table.cells)
                    {
                        cell.rect = D2D1::RectF(
                            table.columnBoundaries[cell.column],
                            table.rowBoundaries[cell.row],
                            table.columnBoundaries[cell.column + 1],
                            table.rowBoundaries[cell.row + 1]);
                        auto verticalInset = (std::max)(0.0f, (cell.rect.bottom - cell.rect.top - cell.textHeight) * 0.5f);
                        cell.textOrigin = D2D1::Point2F(cell.rect.left + 10.0f, cell.rect.top + verticalInset);
                        cell.textWidth = (std::max)(1.0f, cell.rect.right - cell.rect.left - 20.0f);
                    }

                    auto tableIndex = visualTables.size();
                    visualTables.push_back(std::move(table));
                    auto& visualTable = visualTables.back();
                    VisualBlock visualBlock;
                    visualBlock.rect = visualTable.rect;
                    visualBlock.sourceStart = visualTable.sourceStart;
                    visualBlock.sourceEnd = visualTable.sourceEnd;
                    visualBlock.documentY = y + scrollOffset;
                    auto visualBlockIndex = visualBlocks.size();
                    visualBlocks.push_back(std::move(visualBlock));

                    d2dContext->FillRectangle(D2D1::RectF(visualTable.rect.left, visualTable.rowBoundaries[0], visualTable.rect.right, visualTable.rowBoundaries[1]), panelBrush.Get());
                    for (std::size_t cellIndex = 0; cellIndex < visualTable.cells.size(); ++cellIndex)
                    {
                        auto& cell = visualTable.cells[cellIndex];
                        if (!selection.is_empty() && selection.start.v < cell.sourceEnd && cell.sourceStart < selection.end.v)
                        {
                            d2dContext->FillRectangle(cell.rect, selectionBrush.Get());
                        }
                        if (cell.layout)
                        {
                            d2dContext->DrawTextLayout(cell.textOrigin, cell.layout.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        }
                        VisualLine line;
                        line.blockIndex = visualBlockIndex;
                        line.tableIndex = tableIndex;
                        line.cellIndex = cellIndex;
                        line.sourceStart = cell.sourceStart;
                        line.sourceEnd = cell.sourceEnd;
                        line.displayStart = 0;
                        line.displayEnd = static_cast<std::uint32_t>(cell.text.size());
                        line.rect = cell.rect;
                        visualLines.push_back(line);
                    }
                    for (auto boundary : visualTable.columnBoundaries)
                    {
                        d2dContext->DrawLine(D2D1::Point2F(boundary, visualTable.rect.top), D2D1::Point2F(boundary, visualTable.rect.bottom), mutedBrush.Get(), 1.0f);
                    }
                    for (auto boundary : visualTable.rowBoundaries)
                    {
                        d2dContext->DrawLine(D2D1::Point2F(visualTable.rect.left, boundary), D2D1::Point2F(visualTable.rect.right, boundary), mutedBrush.Get(), 1.0f);
                    }
                    y = visualTable.rect.bottom;
                    previousBlank = false;
                    continue;
                }
            }
            IDWriteTextFormat* format = textFormat.Get();
            ID2D1Brush* brush = textBrush.Get();
            float height = 48.0f;
            float inset = 0.0f;
            float textTop = 4.0f;
            bool fillPanel = false;
            bool measureHeight = true;
            std::u32string text;
            std::vector<InlineStyleRange> inlineRanges;
            std::vector<std::size_t> displayToSource;
            std::vector<DisplayInlineText::MathOverlay> inlineMathOverlays;
            std::optional<MathJaxSvg> blockMath;
            bool showRawMath = false;

            switch (block.kind)
            {
                case elmd::RenderBlockKind::Blank:
                    text = U" ";
                    displayToSource.push_back(block.content_range.start.v);
                    displayToSource.push_back(block.content_range.start.v);
                    height = styleSheet.body.lineHeight;
                    textTop = 0.0f;
                    measureHeight = false;
                    break;
                case elmd::RenderBlockKind::Text:
                {
                    auto display = BuildDisplayInlineText(
                        block.inline_items,
                        caret,
                        block.content_range.end.v,
                        sourceText,
                        mathJax,
                        styleSheet.body.size,
                        documentRight - documentLeft,
                        svgSupported);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inlineMathOverlays = std::move(display.mathOverlays);
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
                }
                case elmd::RenderBlockKind::Code:
                {
                    auto display = BuildCodeBlockText(block, caret, sourceText);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    format = codeFormat.Get();
                    brush = codeBrush.Get();
                    height = 64.0f;
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                }
                case elmd::RenderBlockKind::Math:
                {
                    blockMath = svgSupported
                        ? mathJax.Render(elmd::cps_to_utf8(block.tex), true, styleSheet.body.size, documentRight - documentLeft)
                        : MathJaxSvg{};
                    showRawMath = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
                    DisplayInlineText display;
                    if (showRawMath || !static_cast<bool>(*blockMath))
                    {
                        auto visibleEnd = block.source_range.end.v;
                        if (visibleEnd > block.source_range.start.v && visibleEnd <= sourceText.size() && sourceText[visibleEnd - 1] == U'\n')
                        {
                            --visibleEnd;
                        }
                        AppendSourceText(display, sourceText, block.source_range.start.v, visibleEnd, elmd::InlineStyle::plain(), false);
                    }
                    else
                    {
                        AppendMathPlaceholder(display, 1, block.source_range.start.v);
                    }
                    display.displayToSource.push_back(block.source_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    format = codeFormat.Get();
                    brush = codeBrush.Get();
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                }
                case elmd::RenderBlockKind::Table:
                    text = U"Table";
                    displayToSource.push_back(block.source_range.start.v);
                    displayToSource.push_back(block.source_range.end.v);
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
                {
                    auto display = BuildDisplayInlineText(
                        block.inline_items,
                        caret,
                        block.content_range.end.v,
                        sourceText,
                        mathJax,
                        styleSheet.body.size,
                        documentRight - documentLeft,
                        svgSupported);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inlineMathOverlays = std::move(display.mathOverlays);
                    brush = mutedBrush.Get();
                    break;
                }
            }

            if (displayToSource.empty())
            {
                auto sourceStart = SourceStart(block);
                if (text.empty())
                {
                    text = U" ";
                    displayToSource.push_back(sourceStart);
                    displayToSource.push_back(SourceEnd(block, text));
                }
                else
                {
                    displayToSource.reserve(text.size() + 1);
                    for (std::size_t index = 0; index < text.size(); ++index)
                    {
                        displayToSource.push_back(sourceStart + index);
                    }
                    displayToSource.push_back(SourceEnd(block, text));
                }
            }
            if (text.empty())
            {
                auto sourceOffset = displayToSource.empty() ? SourceStart(block) : displayToSource.front();
                text = U" ";
                displayToSource.clear();
                displayToSource.push_back(sourceOffset);
                displayToSource.push_back(sourceOffset);
            }

            auto wide = ToWide(text);
            auto textWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
            auto layout = createLayout(wide, format, textWidth);
            applyInlineStyles(layout.Get(), inlineRanges);
            std::optional<D2D1_POINT_2F> blockMathOrigin;
            if (measureHeight)
            {
                auto fallbackHeight = format == codeFormat.Get() ? styleSheet.code.lineHeight : styleSheet.body.lineHeight;
                auto bottomPadding = fillPanel ? 16.0f : 8.0f;
                height = textTop + measureTextHeight(layout.Get(), fallbackHeight) + bottomPadding;
            }
            if (block.kind == elmd::RenderBlockKind::Math && blockMath && static_cast<bool>(*blockMath))
            {
                auto previewWidth = blockMath->width;
                auto previewX = documentLeft + (std::max)(inset, (documentRight - documentLeft - previewWidth) * 0.5f);
                if (showRawMath)
                {
                    auto rawHeight = measureTextHeight(layout.Get(), styleSheet.code.lineHeight);
                    auto previewY = y + textTop + rawHeight + 10.0f;
                    height = textTop + rawHeight + 10.0f + blockMath->height + 16.0f;
                    blockMathOrigin = D2D1::Point2F(previewX, previewY);
                }
                else
                {
                    height = blockMath->height + 32.0f;
                    blockMathOrigin = D2D1::Point2F(previewX, y + 16.0f);
                }
            }
            auto sourceStart = displayToSource.empty() ? SourceStart(block) : displayToSource.front();
            auto sourceEnd = displayToSource.empty() ? SourceEnd(block, text) : displayToSource.back();
            if (fillPanel)
            {
                d2dContext->FillRectangle(D2D1::RectF(documentLeft, y, documentRight, y + height), panelBrush.Get());
            }
            auto origin = D2D1::Point2F(documentLeft + inset, y + textTop);
            if (layout)
            {
                for (auto const& range : inlineRanges)
                {
                    if (!range.style.code || range.length == 0)
                    {
                        continue;
                    }

                    UINT32 actualCount = 0;
                    auto hr = layout->HitTestTextRange(range.start, range.length, origin.x, origin.y, nullptr, 0, &actualCount);
                    if (hr == E_NOT_SUFFICIENT_BUFFER && actualCount > 0)
                    {
                        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                        if (SUCCEEDED(layout->HitTestTextRange(range.start, range.length, origin.x, origin.y, metrics.data(), actualCount, &actualCount)))
                        {
                            for (UINT32 index = 0; index < actualCount; ++index)
                            {
                                auto const& metric = metrics[index];
                                auto rect = D2D1::RoundedRect(
                                    D2D1::RectF(metric.left - 3.0f, metric.top + 2.0f, metric.left + metric.width + 3.0f, metric.top + metric.height - 1.0f),
                                    4.0f,
                                    4.0f);
                                d2dContext->FillRoundedRectangle(rect, panelBrush.Get());
                            }
                        }
                    }
                }
                if (!selection.is_empty() && selection.end.v > sourceStart && selection.start.v < sourceEnd)
                {
                    auto rangeStart = DisplayPositionForSource(displayToSource, (std::max)(selection.start.v, sourceStart));
                    auto rangeEnd = DisplayPositionForSource(displayToSource, (std::min)(selection.end.v, sourceEnd));
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

                for (auto const& overlay : inlineMathOverlays)
                {
                    float pointX = 0.0f;
                    float pointY = 0.0f;
                    DWRITE_HIT_TEST_METRICS metrics{};
                    if (SUCCEEDED(layout->HitTestTextPosition(overlay.displayStart, FALSE, &pointX, &pointY, &metrics)))
                    {
                        auto mathY = origin.y + pointY + (metrics.height - overlay.svg.height) * 0.5f;
                        drawMathSvg(overlay.svg, D2D1::Point2F(origin.x + pointX, mathY), styleSheet.textColor);
                    }
                }
                if (blockMathOrigin && blockMath)
                {
                    drawMathSvg(*blockMath, *blockMathOrigin, styleSheet.textColor);
                }

                VisualBlock visualBlock;
                visualBlock.rect = D2D1::RectF(documentLeft, y, documentRight, y + height);
                visualBlock.textOrigin = origin;
                visualBlock.textWidth = textWidth;
                visualBlock.sourceStart = sourceStart;
                visualBlock.sourceEnd = sourceEnd;
                visualBlock.documentY = y + scrollOffset;
                visualBlock.text = std::move(text);
                visualBlock.displayToSource = std::move(displayToSource);
                visualBlock.layout = layout;
                visualBlocks.push_back(std::move(visualBlock));
                addVisualLinesForBlock(visualBlocks.size() - 1);
            }
            y += height;
            previousBlank = currentBlank;
        }

        auto drawPlus = [&](D2D1_POINT_2F center)
        {
            d2dContext->FillEllipse(D2D1::Ellipse(center, 9.0f, 9.0f), accentBrush.Get());
            d2dContext->DrawLine(D2D1::Point2F(center.x - 4.0f, center.y), D2D1::Point2F(center.x + 4.0f, center.y), textBrush.Get(), 1.5f);
            d2dContext->DrawLine(D2D1::Point2F(center.x, center.y - 4.0f), D2D1::Point2F(center.x, center.y + 4.0f), textBrush.Get(), 1.5f);
        };
        auto drawRowControls = [&](VisualTable const& table, std::size_t row)
        {
            auto centerY = (table.rowBoundaries[row] + table.rowBoundaries[row + 1]) * 0.5f;
            auto dragRect = D2D1::RectF(table.rect.left - 50.0f, centerY - 11.0f, table.rect.left - 28.0f, centerY + 11.0f);
            auto deleteRect = D2D1::RectF(table.rect.left - 25.0f, centerY - 11.0f, table.rect.left - 3.0f, centerY + 11.0f);
            d2dContext->FillRectangle(dragRect, panelBrush.Get());
            d2dContext->FillRectangle(deleteRect, panelBrush.Get());
            d2dContext->DrawRectangle(dragRect, mutedBrush.Get(), 1.0f);
            d2dContext->DrawRectangle(deleteRect, mutedBrush.Get(), 1.0f);
            for (int index = -1; index <= 1; ++index)
            {
                auto lineY = centerY + static_cast<float>(index * 4);
                d2dContext->DrawLine(D2D1::Point2F(dragRect.left + 6.0f, lineY), D2D1::Point2F(dragRect.right - 6.0f, lineY), mutedBrush.Get(), 1.5f);
            }
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.left + 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.right - 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.right - 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.left + 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
        };
        auto drawColumnControls = [&](VisualTable const& table, std::size_t column)
        {
            auto centerX = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
            auto dragRect = D2D1::RectF(centerX - 23.0f, table.rect.top - 29.0f, centerX - 1.0f, table.rect.top - 7.0f);
            auto deleteRect = D2D1::RectF(centerX + 2.0f, table.rect.top - 29.0f, centerX + 24.0f, table.rect.top - 7.0f);
            d2dContext->FillRectangle(dragRect, panelBrush.Get());
            d2dContext->FillRectangle(deleteRect, panelBrush.Get());
            d2dContext->DrawRectangle(dragRect, mutedBrush.Get(), 1.0f);
            d2dContext->DrawRectangle(deleteRect, mutedBrush.Get(), 1.0f);
            for (int index = -1; index <= 1; ++index)
            {
                auto lineY = (dragRect.top + dragRect.bottom) * 0.5f + static_cast<float>(index * 4);
                d2dContext->DrawLine(D2D1::Point2F(dragRect.left + 6.0f, lineY), D2D1::Point2F(dragRect.right - 6.0f, lineY), mutedBrush.Get(), 1.5f);
            }
            auto centerY = (deleteRect.top + deleteRect.bottom) * 0.5f;
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.left + 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.right - 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.right - 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.left + 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
        };
        if (pointerPosition)
        {
            for (auto const& table : visualTables)
            {
                auto pointer = *pointerPosition;
                if (table.rect.top <= pointer.y && pointer.y <= table.rect.bottom && table.rect.left - 56.0f <= pointer.x && pointer.x <= table.rect.left + 8.0f)
                {
                    for (std::size_t row = 0; row < table.rowCount; ++row)
                    {
                        if (table.rowBoundaries[row] <= pointer.y && pointer.y <= table.rowBoundaries[row + 1]) drawRowControls(table, row);
                    }
                }
                if (table.rect.left <= pointer.x && pointer.x <= table.rect.right && table.rect.top - 35.0f <= pointer.y && pointer.y <= table.rect.top + 8.0f)
                {
                    for (std::size_t column = 0; column < table.columnCount; ++column)
                    {
                        if (table.columnBoundaries[column] <= pointer.x && pointer.x <= table.columnBoundaries[column + 1]) drawColumnControls(table, column);
                    }
                }
                if (table.rect.left <= pointer.x && pointer.x <= table.rect.right)
                {
                    std::size_t column = table.columnCount - 1;
                    for (std::size_t index = 0; index < table.columnCount; ++index)
                    {
                        if (table.columnBoundaries[index] <= pointer.x && pointer.x <= table.columnBoundaries[index + 1]) { column = index; break; }
                    }
                    for (auto boundary : table.rowBoundaries)
                    {
                        if (std::fabs(pointer.y - boundary) <= 10.0f)
                        {
                            auto left = table.columnBoundaries[column];
                            auto right = table.columnBoundaries[column + 1];
                            d2dContext->DrawLine(D2D1::Point2F(left, boundary), D2D1::Point2F(right, boundary), accentBrush.Get(), 2.0f);
                            drawPlus(D2D1::Point2F((left + right) * 0.5f, boundary));
                        }
                    }
                }
                if (table.rect.top <= pointer.y && pointer.y <= table.rect.bottom)
                {
                    std::size_t row = table.rowCount - 1;
                    for (std::size_t index = 0; index < table.rowCount; ++index)
                    {
                        if (table.rowBoundaries[index] <= pointer.y && pointer.y <= table.rowBoundaries[index + 1]) { row = index; break; }
                    }
                    for (auto boundary : table.columnBoundaries)
                    {
                        if (std::fabs(pointer.x - boundary) <= 10.0f)
                        {
                            auto top = table.rowBoundaries[row];
                            auto bottom = table.rowBoundaries[row + 1];
                            d2dContext->DrawLine(D2D1::Point2F(boundary, top), D2D1::Point2F(boundary, bottom), accentBrush.Get(), 2.0f);
                            drawPlus(D2D1::Point2F(boundary, (top + bottom) * 0.5f));
                        }
                    }
                }
            }
        }
        if (draggedTableAction && tableDropIndex)
        {
            for (auto const& table : visualTables)
            {
                if (draggedTableAction->sourceOffset < table.sourceStart || draggedTableAction->sourceOffset > table.sourceEnd) continue;
                if (draggedTableAction->kind == TableActionKind::DragRow && *tableDropIndex < table.rowBoundaries.size())
                {
                    auto boundary = table.rowBoundaries[*tableDropIndex];
                    d2dContext->DrawLine(D2D1::Point2F(table.rect.left, boundary), D2D1::Point2F(table.rect.right, boundary), accentBrush.Get(), 3.0f);
                }
                if (draggedTableAction->kind == TableActionKind::DragColumn && *tableDropIndex < table.columnBoundaries.size())
                {
                    auto boundary = table.columnBoundaries[*tableDropIndex];
                    d2dContext->DrawLine(D2D1::Point2F(boundary, table.rect.top), D2D1::Point2F(boundary, table.rect.bottom), accentBrush.Get(), 3.0f);
                }
            }
        }

        totalDocumentHeight = y + scrollOffset + styleSheet.verticalPadding;

        if (sessionCore.editor.selection().is_caret())
        {
            auto upstream = sessionCore.editor.selection().affinity == elmd::TextAffinity::Upstream;
            if (auto rect = CaretBounds(caret, upstream))
            {
                d2dContext->DrawLine(D2D1::Point2F(rect->left, rect->top), D2D1::Point2F(rect->left, rect->bottom), caretBrush.Get(), 1.5f);
            }
        }
    }

    void EditorSurfaceRenderer::ScrollBy(float delta)
    {
        auto maxScroll = (std::max)(0.0f, totalDocumentHeight - surfaceHeightDip);
        scrollOffset = (std::min)(maxScroll, (std::max)(0.0f, scrollOffset + delta));
    }

    void EditorSurfaceRenderer::UpdatePointer(float x, float y)
    {
        pointerPosition = D2D1::Point2F(x, y);
    }

    void EditorSurfaceRenderer::ClearPointer()
    {
        pointerPosition.reset();
    }

    void EditorSurfaceRenderer::SetTableDrag(std::optional<TableAction> action, std::optional<std::size_t> dropIndex)
    {
        draggedTableAction = std::move(action);
        tableDropIndex = dropIndex;
    }

    std::optional<EditorSurfaceRenderer::TableAction> EditorSurfaceRenderer::TableActionAt(float x, float y) const
    {
        for (auto const& table : visualTables)
        {
            auto source_for_row = [&](std::size_t row) {
                auto index = (std::min)(row, table.rowCount - 1) * table.columnCount;
                return index < table.cells.size() ? table.cells[index].sourceStart : table.sourceStart;
            };
            auto source_for_column = [&](std::size_t column) {
                auto index = (std::min)(column, table.columnCount - 1);
                return index < table.cells.size() ? table.cells[index].sourceStart : table.sourceStart;
            };
            for (std::size_t boundary = 0; boundary < table.rowBoundaries.size(); ++boundary)
            {
                for (std::size_t column = 0; column < table.columnCount; ++column)
                {
                    auto centerX = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
                    auto centerY = table.rowBoundaries[boundary];
                    auto dx = x - centerX;
                    auto dy = y - centerY;
                    if (dx * dx + dy * dy <= 81.0f)
                    {
                        auto source = source_for_row(boundary == table.rowCount ? table.rowCount - 1 : boundary);
                        return TableAction{TableActionKind::InsertRow, source, boundary};
                    }
                }
            }
            for (std::size_t boundary = 0; boundary < table.columnBoundaries.size(); ++boundary)
            {
                for (std::size_t row = 0; row < table.rowCount; ++row)
                {
                    auto centerX = table.columnBoundaries[boundary];
                    auto centerY = (table.rowBoundaries[row] + table.rowBoundaries[row + 1]) * 0.5f;
                    auto dx = x - centerX;
                    auto dy = y - centerY;
                    if (dx * dx + dy * dy <= 81.0f)
                    {
                        auto source = source_for_column(boundary == table.columnCount ? table.columnCount - 1 : boundary);
                        return TableAction{TableActionKind::InsertColumn, source, boundary};
                    }
                }
            }
            if (table.rect.left - 52.0f <= x && x <= table.rect.left - 3.0f && table.rect.top <= y && y <= table.rect.bottom)
            {
                for (std::size_t row = 0; row < table.rowCount; ++row)
                {
                    if (table.rowBoundaries[row] > y || y > table.rowBoundaries[row + 1]) continue;
                    auto source = source_for_row(row);
                    if (x <= table.rect.left - 28.0f) return TableAction{TableActionKind::DragRow, source, row};
                    return TableAction{TableActionKind::DeleteRow, source, row};
                }
            }
            if (table.rect.top - 29.0f <= y && y <= table.rect.top - 7.0f && table.rect.left <= x && x <= table.rect.right)
            {
                for (std::size_t column = 0; column < table.columnCount; ++column)
                {
                    auto center = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
                    if (center - 23.0f <= x && x <= center - 1.0f) return TableAction{TableActionKind::DragColumn, source_for_column(column), column};
                    if (center + 2.0f <= x && x <= center + 24.0f) return TableAction{TableActionKind::DeleteColumn, source_for_column(column), column};
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::TableDropIndexAt(float x, float y, bool rows) const
    {
        for (auto const& table : visualTables)
        {
            if (draggedTableAction && (draggedTableAction->sourceOffset < table.sourceStart || draggedTableAction->sourceOffset > table.sourceEnd)) continue;
            if (rows)
            {
                if (x < table.rect.left - 60.0f || x > table.rect.right + 20.0f) continue;
                if (y <= table.rect.top) return 0;
                if (y >= table.rect.bottom) return table.rowCount;
                std::size_t nearest = 0;
                float distance = (std::numeric_limits<float>::max)();
                for (std::size_t index = 0; index < table.rowBoundaries.size(); ++index)
                {
                    auto candidate = std::fabs(y - table.rowBoundaries[index]);
                    if (candidate < distance) { distance = candidate; nearest = index; }
                }
                return nearest;
            }
            if (y < table.rect.top - 40.0f || y > table.rect.bottom + 20.0f) continue;
            if (x <= table.rect.left) return 0;
            if (x >= table.rect.right) return table.columnCount;
            std::size_t nearest = 0;
            float distance = (std::numeric_limits<float>::max)();
            for (std::size_t index = 0; index < table.columnBoundaries.size(); ++index)
            {
                auto candidate = std::fabs(x - table.columnBoundaries[index]);
                if (candidate < distance) { distance = candidate; nearest = index; }
            }
            return nearest;
        }
        return std::nullopt;
    }

    void EditorSurfaceRenderer::ScrollToSourceOffset(std::size_t sourceOffset)
    {
        if (auto caretBounds = CaretBounds(sourceOffset))
        {
            auto margin = styleSheet.verticalPadding;
            auto maxScroll = (std::max)(0.0f, totalDocumentHeight - surfaceHeightDip);
            if (caretBounds->top < margin)
            {
                scrollOffset = (std::max)(0.0f, scrollOffset - (margin - caretBounds->top));
            }
            else if (caretBounds->bottom > surfaceHeightDip - margin)
            {
                scrollOffset = (std::min)(maxScroll, scrollOffset + caretBounds->bottom - (surfaceHeightDip - margin));
            }
            return;
        }

        for (auto const& block : visualBlocks)
        {
            if (block.sourceStart <= sourceOffset && sourceOffset <= block.sourceEnd)
            {
                auto maxScroll = (std::max)(0.0f, totalDocumentHeight - surfaceHeightDip);
                scrollOffset = (std::min)(maxScroll, (std::max)(0.0f, block.documentY - styleSheet.verticalPadding));
                return;
            }
        }
    }

    std::optional<std::size_t> EditorSurfaceRenderer::LineIndexFor(std::size_t sourceOffset, bool upstream) const
    {
        if (visualLines.empty())
        {
            return std::nullopt;
        }

        std::optional<std::size_t> firstContaining;
        std::optional<std::size_t> lastContaining;
        for (std::size_t index = 0; index < visualLines.size(); ++index)
        {
            auto const& line = visualLines[index];
            if (line.sourceStart <= sourceOffset && sourceOffset <= line.sourceEnd)
            {
                if (!firstContaining)
                {
                    firstContaining = index;
                }
                lastContaining = index;
            }
        }
        if (firstContaining)
        {
            return upstream ? firstContaining : lastContaining;
        }

        std::optional<std::size_t> prev;
        std::optional<std::size_t> next;
        for (std::size_t index = 0; index < visualLines.size(); ++index)
        {
            if (visualLines[index].sourceEnd <= sourceOffset)
            {
                prev = index;
            }
            if (!next && visualLines[index].sourceStart >= sourceOffset)
            {
                next = index;
            }
        }
        if (prev && next)
        {
            auto distPrev = sourceOffset - visualLines[*prev].sourceEnd;
            auto distNext = visualLines[*next].sourceStart - sourceOffset;
            if (distPrev == distNext)
            {
                return upstream ? prev : next;
            }
            return distPrev <= distNext ? prev : next;
        }
        if (prev)
        {
            return prev;
        }
        return next;
    }

    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretRectOnLine(VisualLine const& line, std::size_t sourceOffset, bool upstream) const
    {
        if (line.blockIndex >= visualBlocks.size())
        {
            return std::nullopt;
        }
        auto const& block = visualBlocks[line.blockIndex];
        auto clamped = (std::min)((std::max)(sourceOffset, line.sourceStart), line.sourceEnd);
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (line.tableIndex < visualTables.size() && line.cellIndex < visualTables[line.tableIndex].cells.size())
        {
            auto const& cell = visualTables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (!layout)
        {
            return D2D1::RectF(line.rect.left, line.rect.top, line.rect.left + 2.0f, line.rect.bottom);
        }

        auto displayPos = DisplayPositionForSource(*displayToSource, clamped);
        displayPos = (std::min)((std::max)(displayPos, static_cast<std::size_t>(line.displayStart)), static_cast<std::size_t>(line.displayEnd));

        UINT32 hitPos = static_cast<UINT32>(displayPos);
        BOOL trailing = FALSE;
        bool atLineEnd = displayPos == line.displayEnd;
        if (atLineEnd && upstream && displayPos > line.displayStart)
        {
            hitPos = static_cast<UINT32>(displayPos - 1);
            trailing = TRUE;
        }

        FLOAT caretX = 0.0f;
        FLOAT caretY = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (SUCCEEDED(layout->HitTestTextPosition(hitPos, trailing, &caretX, &caretY, &metrics)))
        {
            auto left = textOrigin.x + caretX;
            auto top = line.rect.top;
            auto bottom = line.rect.bottom;
            return D2D1::RectF(left, top, left + 2.0f, bottom);
        }
        return D2D1::RectF(line.rect.left, line.rect.top, line.rect.left + 2.0f, line.rect.bottom);
    }

    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index)
        {
            return std::nullopt;
        }
        return CaretRectOnLine(visualLines[*index], sourceOffset, upstream);
    }

    std::optional<std::size_t> EditorSurfaceRenderer::VisualLineStart(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index)
        {
            return std::nullopt;
        }
        return visualLines[*index].sourceStart;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::VisualLineEnd(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index)
        {
            return std::nullopt;
        }
        return visualLines[*index].sourceEnd;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::HitTest(float x, float y, bool* outUpstream) const
    {
        if (outUpstream)
        {
            *outUpstream = false;
        }
        if (visualLines.empty())
        {
            return std::nullopt;
        }

        std::size_t best = 0;
        float bestDist = (std::numeric_limits<float>::max)();
        for (std::size_t index = 0; index < visualLines.size(); ++index)
        {
            auto const& rect = visualLines[index].rect;
            float dist = 0.0f;
            if (y < rect.top)
            {
                dist = rect.top - y;
            }
            else if (y > rect.bottom)
            {
                dist = y - rect.bottom;
            }
            if (visualLines[index].tableIndex < visualTables.size())
            {
                if (x < rect.left) dist += rect.left - x;
                else if (x > rect.right) dist += x - rect.right;
            }
            if (dist < bestDist)
            {
                bestDist = dist;
                best = index;
            }
            if (dist == 0.0f)
            {
                break;
            }
        }

        auto const& line = visualLines[best];
        auto const& block = visualBlocks[line.blockIndex];
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (line.tableIndex < visualTables.size() && line.cellIndex < visualTables[line.tableIndex].cells.size())
        {
            auto const& cell = visualTables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (!layout)
        {
            if (outUpstream)
            {
                *outUpstream = false;
            }
            return line.sourceStart;
        }

        BOOL isTrailingHit = FALSE;
        BOOL isInside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        float localX = x - textOrigin.x;
        float localY = (line.rect.top + line.rect.bottom) * 0.5f - textOrigin.y;
        if (SUCCEEDED(layout->HitTestPoint(localX, localY, &isTrailingHit, &isInside, &metrics)))
        {
            auto displayPos = static_cast<std::size_t>(metrics.textPosition + (isTrailingHit ? metrics.length : 0));
            displayPos = (std::min)((std::max)(displayPos, static_cast<std::size_t>(line.displayStart)), static_cast<std::size_t>(line.displayEnd));
            std::size_t srcOff = displayPos < displayToSource->size() ? (*displayToSource)[displayPos] : line.sourceEnd;
            srcOff = (std::min)((std::max)(srcOff, line.sourceStart), line.sourceEnd);
            if (outUpstream)
            {
                bool nextIsWrap = best + 1 < visualLines.size()
                    && visualLines[best + 1].blockIndex == line.blockIndex
                    && visualLines[best + 1].wrapContinuation
                    && visualLines[best + 1].displayStart == line.displayEnd;
                *outUpstream = srcOff == line.sourceEnd && nextIsWrap;
            }
            return srcOff;
        }

        if (outUpstream)
        {
            *outUpstream = false;
        }
        return line.sourceStart;
    }

    std::optional<EditorSurfaceRenderer::CaretMove> EditorSurfaceRenderer::MoveCaretVertically(std::size_t sourceOffset, bool upstream, bool down, float& goalX) const
    {
        auto current = LineIndexFor(sourceOffset, upstream);
        if (!current || visualLines.empty())
        {
            return std::nullopt;
        }

        float x = goalX;
        if (x < 0.0f)
        {
            if (auto rect = CaretRectOnLine(visualLines[*current], sourceOffset, upstream))
            {
                x = rect->left;
            }
            else
            {
                x = visualLines[*current].rect.left;
            }
            goalX = x;
        }

        if (!down && *current == 0)
        {
            return CaretMove{ visualLines.front().sourceStart, false };
        }
        if (down && *current + 1 >= visualLines.size())
        {
            return CaretMove{ visualLines.back().sourceEnd, true };
        }

        std::size_t targetIndex = down ? *current + 1 : *current - 1;
        auto const& currentLine = visualLines[*current];
        if (currentLine.tableIndex < visualTables.size() && currentLine.cellIndex < visualTables[currentLine.tableIndex].cells.size())
        {
            auto const& table = visualTables[currentLine.tableIndex];
            auto const& cell = table.cells[currentLine.cellIndex];
            if (down && cell.row + 1 < table.rowCount)
            {
                auto targetCell = (cell.row + 1) * table.columnCount + cell.column;
                for (std::size_t index = 0; index < visualLines.size(); ++index)
                {
                    if (visualLines[index].tableIndex == currentLine.tableIndex && visualLines[index].cellIndex == targetCell) { targetIndex = index; break; }
                }
            }
            else if (!down && cell.row > 0)
            {
                auto targetCell = (cell.row - 1) * table.columnCount + cell.column;
                for (std::size_t index = 0; index < visualLines.size(); ++index)
                {
                    if (visualLines[index].tableIndex == currentLine.tableIndex && visualLines[index].cellIndex == targetCell) { targetIndex = index; break; }
                }
            }
            else if (down)
            {
                auto lastCell = table.rowCount * table.columnCount - 1;
                for (std::size_t index = *current; index < visualLines.size(); ++index)
                {
                    if (visualLines[index].tableIndex == currentLine.tableIndex && visualLines[index].cellIndex == lastCell)
                    {
                        targetIndex = (std::min)(index + 1, visualLines.size() - 1);
                        break;
                    }
                }
            }
            else
            {
                for (std::size_t index = *current; index > 0; --index)
                {
                    if (visualLines[index - 1].tableIndex != currentLine.tableIndex) { targetIndex = index - 1; break; }
                }
            }
        }
        auto const& target = visualLines[targetIndex];
        auto const& block = visualBlocks[target.blockIndex];

        std::size_t srcOff = target.sourceStart;
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (target.tableIndex < visualTables.size() && target.cellIndex < visualTables[target.tableIndex].cells.size())
        {
            auto const& cell = visualTables[target.tableIndex].cells[target.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (layout)
        {
            float targetX = (std::min)((std::max)(x, target.rect.left), target.rect.right - 1.0f);
            float localX = targetX - textOrigin.x;
            float localY = (target.rect.top + target.rect.bottom) * 0.5f - textOrigin.y;
            BOOL isTrailingHit = FALSE;
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(layout->HitTestPoint(localX, localY, &isTrailingHit, &isInside, &metrics)))
            {
                auto displayPos = static_cast<std::size_t>(metrics.textPosition + (isTrailingHit ? metrics.length : 0));
                displayPos = (std::min)((std::max)(displayPos, static_cast<std::size_t>(target.displayStart)), static_cast<std::size_t>(target.displayEnd));
                srcOff = displayPos < displayToSource->size() ? (*displayToSource)[displayPos] : target.sourceEnd;
            }
        }
        srcOff = (std::min)((std::max)(srcOff, target.sourceStart), target.sourceEnd);
        bool newUpstream = false;
        auto downstreamLine = LineIndexFor(srcOff, false);
        auto upstreamLine = LineIndexFor(srcOff, true);
        if ((!downstreamLine || *downstreamLine != targetIndex) && upstreamLine && *upstreamLine == targetIndex) newUpstream = true;
        return CaretMove{ srcOff, newUpstream };
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
            properties.dpiX = 96.0f * surfaceScaleX;
            properties.dpiY = 96.0f * surfaceScaleY;
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

            winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties, d2dTarget.GetAddressOf()));
            d2dContext->SetTarget(d2dTarget.Get());
            d2dContext->SetDpi(96.0f * surfaceScaleX, 96.0f * surfaceScaleY);
        }

        if (!textBrush)
        {
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.textColor, textBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.mutedColor, mutedBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.accentColor, accentBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.codeTextColor, codeBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.panelColor, panelBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.selectionColor, selectionBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.caretColor, caretBrush.GetAddressOf()));
        }

        d2dContext->BeginDraw();
        d2dContext->Clear(styleSheet.canvasColor);
        DrawDocument(sessionCore);
        winrt::check_hresult(d2dContext->EndDraw());

        winrt::check_hresult(swapChain->Present(1, 0));
    }
}

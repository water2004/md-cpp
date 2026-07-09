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
            if (row + 1 < block.row_count) text.push_back(U'\n');
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
        std::u32string text;
        std::vector<std::size_t> displayToSource;
        std::vector<InlineStyleRange> ranges;
    };

    DisplayInlineText BuildTableText(elmd::RenderBlock const& block, std::u32string const& sourceText)
    {
        DisplayInlineText display;
        if (auto table = elmd::table_source_at(sourceText, block.source_range.start.v))
        {
            auto end = table->range.end.v;
            if (table->trailing_newline && end > table->range.start.v)
            {
                --end;
            }
            display.text = std::u32string(sourceText.begin() + table->range.start.v, sourceText.begin() + end);
            display.displayToSource.reserve(display.text.size() + 1);
            for (std::size_t index = 0; index < display.text.size(); ++index)
            {
                display.displayToSource.push_back(table->range.start.v + index);
            }
            display.displayToSource.push_back(end);
            return display;
        }

        display.text = TableText(block);
        display.displayToSource.reserve(display.text.size() + 1);
        for (std::size_t index = 0; index < display.text.size(); ++index)
        {
            display.displayToSource.push_back(block.source_range.start.v + index);
        }
        display.displayToSource.push_back(block.source_range.end.v);
        return display;
    }

    bool IsStyleMarker(elmd::InlineRenderItem const& item)
    {
        return item.kind == elmd::InlineRenderItem::Kind::Marker
            && (item.text == U"*" || item.text == U"**" || item.text == U"~~" || item.text == U"`");
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

    DisplayInlineText BuildDisplayInlineText(std::vector<elmd::InlineRenderItem> const& items, std::size_t caret, std::size_t sourceEnd)
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
            AppendDisplayText(display, InlineText(item), item.source_range.start.v, item.style, item.kind == elmd::InlineRenderItem::Kind::Marker);
        }
        display.displayToSource.push_back(sourceEnd);
        return display;
    }

    DisplayInlineText BuildCodeBlockText(elmd::RenderBlock const& block, std::size_t caret)
    {
        DisplayInlineText display;
        auto showFence = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
        if (showFence)
        {
            std::u32string opening = U"```";
            if (block.language)
            {
                opening += elmd::utf8_to_cps(*block.language);
            }
            opening.push_back(U'\n');
            AppendDisplayText(display, opening, block.source_range.start.v, elmd::InlineStyle::plain(), true);
        }
        AppendDisplayText(display, block.code_text, block.content_range.start.v, elmd::InlineStyle::plain(), false);
        if (showFence)
        {
            AppendDisplayText(display, U"```", block.content_range.end.v, elmd::InlineStyle::plain(), true);
        }
        display.displayToSource.push_back(showFence ? block.source_range.end.v : block.content_range.end.v);
        return display;
    }

    EditorSurfaceRenderer::EditorStyleSheet EditorSurfaceRenderer::CreateStyleSheet(Theme value)
    {
        EditorStyleSheet sheet;
        sheet.body = FontStyle{ L"Microsoft YaHei UI", 18.0f, 31.0f, DWRITE_FONT_WEIGHT_NORMAL };
        sheet.heading1 = FontStyle{ L"Microsoft YaHei UI", 38.0f, 50.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading2 = FontStyle{ L"Microsoft YaHei UI", 30.0f, 42.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading3 = FontStyle{ L"Microsoft YaHei UI", 24.0f, 35.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
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
        auto documentLeft = styleSheet.horizontalPadding;
        auto documentTop = styleSheet.verticalPadding;
        auto documentRight = (std::min)(surfaceWidthDip - styleSheet.horizontalPadding, documentLeft + styleSheet.documentWidth);
        auto y = documentTop - scrollOffset;
        auto selection = sessionCore.editor.selection().normalized_range();
        auto caret = sessionCore.editor.selection().active.v;
        auto sourceText = sessionCore.editor.text_cps();

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
                visualLine.rect = D2D1::RectF(block.textOrigin.x, lineTop, block.textOrigin.x + block.textWidth, lineTop + line.height);
                visualLines.push_back(visualLine);
                textPosition = lineEndPosition;
                lineTop += line.height;
            }
        };

        if (sessionCore.renderModel.blocks.empty() && sourceText.empty())
        {
            auto emptyText = winrt::hstring(L"Open a Markdown file or start editing to see the WYSIWYG surface.");
            auto rect = D2D1::RectF(documentLeft, y, documentRight, y + 80.0f);
            d2dContext->DrawTextW(emptyText.c_str(), static_cast<UINT32>(emptyText.size()), textFormat.Get(), rect, mutedBrush.Get());
            return;
        }

        for (std::size_t blockIndex = 0; blockIndex < sessionCore.renderModel.blocks.size(); ++blockIndex)
        {
            auto const& block = sessionCore.renderModel.blocks[blockIndex];
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

            switch (block.kind)
            {
                case elmd::RenderBlockKind::Text:
                {
                    auto display = BuildDisplayInlineText(block.inline_items, caret, block.content_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
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
                    auto display = BuildCodeBlockText(block, caret);
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
                    text = U"$$\n" + block.tex + U"\n$$";
                    format = codeFormat.Get();
                    brush = accentBrush.Get();
                    height = 64.0f;
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                case elmd::RenderBlockKind::Table:
                {
                    auto display = BuildTableText(block, sourceText);
                    text = std::move(display.text);
                    displayToSource = std::move(display.displayToSource);
                    format = codeFormat.Get();
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                }
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
                    auto display = BuildDisplayInlineText(block.inline_items, caret, block.content_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    brush = mutedBrush.Get();
                    break;
                }
            }

            if (displayToSource.empty())
            {
                auto sourceStart = SourceStart(block);
                displayToSource.reserve(text.size() + 1);
                for (std::size_t index = 0; index < text.size(); ++index)
                {
                    displayToSource.push_back(sourceStart + index);
                }
                displayToSource.push_back(SourceEnd(block, text));
            }

            auto wide = ToWide(text);
            auto textWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
            auto layout = createLayout(wide, format, textWidth);
            applyInlineStyles(layout.Get(), inlineRanges);
            if (measureHeight)
            {
                auto fallbackHeight = format == codeFormat.Get() ? styleSheet.code.lineHeight : styleSheet.body.lineHeight;
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
                auto sourceStart = displayToSource.empty() ? SourceStart(block) : displayToSource.front();
                auto sourceEnd = displayToSource.empty() ? SourceEnd(block, text) : displayToSource.back();
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

                auto caretAtHiddenBlockNewline = caret == sourceEnd && sourceEnd > sourceStart && sourceEnd <= sourceText.size() && sourceText[sourceEnd - 1] == U'\n';
                if (sourceStart <= caret && caret <= sourceEnd && !caretAtHiddenBlockNewline)
                {
                    auto caretPosition = DisplayPositionForSource(displayToSource, caret);
                    FLOAT caretX = 0.0f;
                    FLOAT caretY = 0.0f;
                    DWRITE_HIT_TEST_METRICS caretMetrics{};
                    if (SUCCEEDED(layout->HitTestTextPosition(static_cast<UINT32>(caretPosition), false, &caretX, &caretY, &caretMetrics)))
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
                visualBlock.displayToSource = std::move(displayToSource);
                visualBlock.layout = layout;
                visualBlocks.push_back(std::move(visualBlock));
                addVisualLinesForBlock(visualBlocks.size() - 1);
            }
            y += height + styleSheet.blockGap;
        }

        totalDocumentHeight = y + scrollOffset + styleSheet.verticalPadding;
    }

    void EditorSurfaceRenderer::ScrollBy(float delta)
    {
        auto maxScroll = (std::max)(0.0f, totalDocumentHeight - surfaceHeightDip);
        scrollOffset = (std::min)(maxScroll, (std::max)(0.0f, scrollOffset + delta));
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

    std::optional<std::size_t> EditorSurfaceRenderer::HitTest(float x, float y) const
    {
        for (auto const& block : visualBlocks)
        {
            if (y < block.rect.top)
            {
                return block.sourceStart;
            }

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
                if (position < block.displayToSource.size())
                {
                    return (std::min)(block.sourceEnd, block.displayToSource[position]);
                }
                return block.sourceEnd;
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

    std::optional<std::size_t> EditorSurfaceRenderer::MoveCaretVertically(std::size_t sourceOffset, bool down) const
    {
        auto caretBounds = CaretBounds(sourceOffset);
        if (!caretBounds || visualLines.empty())
        {
            return std::nullopt;
        }

        auto x = caretBounds->left;
        auto caretCenterY = (caretBounds->top + caretBounds->bottom) * 0.5f;
        auto currentLineIndex = std::optional<std::size_t>{};
        for (std::size_t index = 0; index < visualLines.size(); ++index)
        {
            auto const& line = visualLines[index];
            if (line.rect.top <= caretCenterY && caretCenterY <= line.rect.bottom && line.sourceStart <= sourceOffset && sourceOffset <= line.sourceEnd)
            {
                currentLineIndex = index;
                break;
            }
        }

        if (!currentLineIndex)
        {
            return std::nullopt;
        }
        if (!down && *currentLineIndex == 0)
        {
            return visualLines.front().sourceStart;
        }
        if (down && *currentLineIndex + 1 >= visualLines.size())
        {
            return visualLines.back().sourceEnd;
        }

        auto const& targetLine = visualLines[*currentLineIndex + (down ? 1 : -1)];
        auto targetX = (std::min)((std::max)(x, targetLine.rect.left), targetLine.rect.right - 1.0f);
        auto targetY = (targetLine.rect.top + targetLine.rect.bottom) * 0.5f;
        return HitTest(targetX, targetY);
    }

    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(std::size_t sourceOffset) const
    {
        for (auto const& block : visualBlocks)
        {
            if (sourceOffset < block.sourceStart || sourceOffset > block.sourceEnd || !block.layout)
            {
                continue;
            }

            FLOAT caretX = 0.0f;
            FLOAT caretY = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            auto position = DisplayPositionForSource(block.displayToSource, sourceOffset);
            if (SUCCEEDED(block.layout->HitTestTextPosition(static_cast<UINT32>(position), false, &caretX, &caretY, &metrics)))
            {
                auto left = block.textOrigin.x + caretX;
                auto top = block.textOrigin.y + caretY;
                return D2D1::RectF(left, top, left + 2.0f, top + metrics.height);
            }
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

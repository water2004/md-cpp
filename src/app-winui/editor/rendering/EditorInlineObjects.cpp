#include "pch.h"
#include "editor/rendering/EditorContentPreparation.h"

namespace winrt::Folia
{
    class MathInlineObject final : public ::Microsoft::WRL::RuntimeClass<::Microsoft::WRL::RuntimeClassFlags<::Microsoft::WRL::ClassicCom>, IDWriteInlineObject>
    {
    public:
        MathInlineObject(float width, float height, float baseline, bool breakBefore = false)
            : width(width), height(height), baseline(baseline), breakBefore(breakBefore) {}

        IFACEMETHODIMP Draw(void*, IDWriteTextRenderer*, FLOAT, FLOAT, BOOL, BOOL, IUnknown*) override
        {
            return S_OK;
        }

        IFACEMETHODIMP GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override
        {
            if (!metrics) return E_POINTER;
            metrics->width = width;
            metrics->height = height;
            metrics->baseline = baseline;
            metrics->supportsSideways = FALSE;
            return S_OK;
        }

        IFACEMETHODIMP GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override
        {
            if (!overhangs) return E_POINTER;
            *overhangs = {};
            return S_OK;
        }

        IFACEMETHODIMP GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override
        {
            if (!before || !after) return E_POINTER;
            *before = breakBefore ? DWRITE_BREAK_CONDITION_CAN_BREAK : DWRITE_BREAK_CONDITION_NEUTRAL;
            *after = DWRITE_BREAK_CONDITION_NEUTRAL;
            return S_OK;
        }

    private:
        float width;
        float height;
        float baseline;
        bool breakBefore;
    };

    class IndentInlineObject final : public ::Microsoft::WRL::RuntimeClass<::Microsoft::WRL::RuntimeClassFlags<::Microsoft::WRL::ClassicCom>, IDWriteInlineObject>
    {
    public:
        IndentInlineObject(float width, float height, float baseline) : width(width), height(height), baseline(baseline) {}

        IFACEMETHODIMP Draw(void*, IDWriteTextRenderer*, FLOAT, FLOAT, BOOL, BOOL, IUnknown*) override
        {
            return S_OK;
        }

        IFACEMETHODIMP GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override
        {
            if (!metrics) return E_POINTER;
            metrics->width = width;
            metrics->height = height;
            metrics->baseline = baseline;
            metrics->supportsSideways = FALSE;
            return S_OK;
        }

        IFACEMETHODIMP GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override
        {
            if (!overhangs) return E_POINTER;
            *overhangs = {};
            return S_OK;
        }

        IFACEMETHODIMP GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override
        {
            if (!before || !after) return E_POINTER;
            *before = DWRITE_BREAK_CONDITION_MUST_BREAK;
            *after = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
            return S_OK;
        }

    private:
        float width;
        float height;
        float baseline;
    };

    void ApplyMathInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::MathOverlay> const& overlays)
    {
        if (!layout) return;
        if (!overlays.empty()) layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0.0f, 0.0f);
        for (auto const& overlay : overlays)
        {
            auto const& fragment = overlay.fragment;
            auto baseline = (std::clamp)(fragment.height + fragment.verticalAlign, 0.0f, fragment.height);
            auto object = ::Microsoft::WRL::Make<MathInlineObject>(
                overlay.leadingSpace + fragment.width,
                fragment.height,
                baseline,
                fragment.breakBefore);
            if (object)
            {
                layout->SetInlineObject(object.Get(), DWRITE_TEXT_RANGE{ overlay.displayStart, 1 });
            }
        }
    }

    void ApplyInlinePlaceholder(IDWriteTextLayout* layout, UINT32 displayStart, float width, float height, float baseline)
    {
        if (!layout) return;
        auto object = ::Microsoft::WRL::Make<MathInlineObject>(width, height, baseline);
        if (object) layout->SetInlineObject(object.Get(), DWRITE_TEXT_RANGE{ displayStart, 1 });
    }

    void ApplyTaskCheckboxInlineObjects(
        IDWriteTextLayout* layout,
        std::vector<DisplayInlineText::TaskCheckboxOverlay> const& overlays)
    {
        if (!layout) return;
        for (auto const& overlay : overlays)
            ApplyInlinePlaceholder(layout, overlay.displayStart, overlay.advance, overlay.height, overlay.baseline);
    }

    void ApplyIndentInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::IndentOverlay> const& overlays)
    {
        if (!layout) return;
        for (auto const& overlay : overlays)
        {
            auto object = ::Microsoft::WRL::Make<IndentInlineObject>(overlay.width, 1.0f, 1.0f);
            if (object) layout->SetInlineObject(object.Get(), DWRITE_TEXT_RANGE{ overlay.displayStart, 1 });
        }
    }

}

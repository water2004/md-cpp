export module elmd.core.layout_plan;
import std;
import elmd.core.ids;

export namespace elmd {

struct BlockLayoutInput {
    BlockId id{};
    float estimated_height = 0.0f;
    bool blank = false;
    bool force_measure = false;
};

struct BlockPlacement {
    BlockId id{};
    std::size_t index = 0;
    float top = 0.0f;
    float height = 0.0f;
    bool measure = false;
    bool request_embedded = false;

    float bottom() const { return top + height; }
};

struct LayoutPlanSettings {
    float document_top = 0.0f;
    float document_bottom_padding = 0.0f;
    float viewport_top = 0.0f;
    float viewport_height = 0.0f;
    float block_gap = 0.0f;
    float blank_gap = 0.0f;
    float measure_overscan = 1.0f;
    float embedded_overscan = 2.0f;
};

struct DocumentLayoutPlan {
    std::vector<BlockPlacement> blocks;
    float total_height = 0.0f;
};

inline DocumentLayoutPlan plan_document_layout(
    std::span<BlockLayoutInput const> inputs,
    LayoutPlanSettings settings)
{
    DocumentLayoutPlan result;
    result.blocks.reserve(inputs.size());
    auto viewport_height = std::isfinite(settings.viewport_height) ? (std::max)(0.0f, settings.viewport_height) : 0.0f;
    auto viewport_top = std::isfinite(settings.viewport_top) ? (std::max)(0.0f, settings.viewport_top) : 0.0f;
    auto measure_overscan = std::isfinite(settings.measure_overscan) ? (std::max)(0.0f, settings.measure_overscan) : 0.0f;
    auto embedded_overscan = std::isfinite(settings.embedded_overscan) ? (std::max)(0.0f, settings.embedded_overscan) : 0.0f;
    auto measure_top = viewport_top - viewport_height * measure_overscan;
    auto measure_bottom = viewport_top + viewport_height * (1.0f + measure_overscan);
    auto embedded_top = viewport_top - viewport_height * embedded_overscan;
    auto embedded_bottom = viewport_top + viewport_height * (1.0f + embedded_overscan);
    auto cursor = std::isfinite(settings.document_top) ? settings.document_top : 0.0f;
    bool previous_blank = false;

    for (std::size_t index = 0; index < inputs.size(); ++index) {
        auto const& input = inputs[index];
        if (index > 0 && !(previous_blank && input.blank)) {
            auto gap = previous_blank || input.blank ? settings.blank_gap : settings.block_gap;
            if (std::isfinite(gap) && gap > 0.0f) cursor += gap;
        }
        auto height = std::isfinite(input.estimated_height) ? (std::max)(0.0f, input.estimated_height) : 0.0f;
        BlockPlacement placement;
        placement.id = input.id;
        placement.index = index;
        placement.top = cursor;
        placement.height = height;
        placement.measure = input.force_measure || placement.bottom() >= measure_top && placement.top <= measure_bottom;
        placement.request_embedded = placement.bottom() >= embedded_top && placement.top <= embedded_bottom;
        result.blocks.push_back(placement);
        cursor += height;
        previous_blank = input.blank;
    }

    auto padding = std::isfinite(settings.document_bottom_padding) ? (std::max)(0.0f, settings.document_bottom_padding) : 0.0f;
    result.total_height = cursor + padding;
    return result;
}

}

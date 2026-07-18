export module folia.core.layout_plan;
import std;
import folia.core.ids;

export namespace folia {

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

struct PrintBlockExtent {
    float top = 0.0f;
    float bottom = 0.0f;
};

struct PrintPageSlice {
    float source_top = 0.0f;
    float source_bottom = 0.0f;

    float height() const { return source_bottom - source_top; }
};

struct PrintPageStep {
    PrintPageSlice slice;
    // Number of leading extents that no longer need to participate in the
    // next step. This remains zero while an oversized block is split across
    // multiple pages.
    std::size_t consumed_blocks = 0;
    float next_source_top = 0.0f;
    bool has_more = false;
};

// Plan only the next page from an already ordered suffix of block extents.
// PDF export uses this form so page N does not rescan or allocate a plan for
// every later page. The result has the same block-splitting semantics as
// plan_print_pages: fitting blocks stay intact and only a block taller than a
// page is split.
inline PrintPageStep plan_next_print_page(
    std::span<PrintBlockExtent const> remaining,
    float document_top,
    float document_bottom,
    float page_extent)
{
    constexpr float epsilon = 0.01f;
    page_extent = std::isfinite(page_extent) ? (std::max)(1.0f, page_extent) : 1.0f;
    document_top = std::isfinite(document_top) ? document_top : 0.0f;
    document_bottom = std::isfinite(document_bottom)
        ? (std::max)(document_top, document_bottom)
        : document_top;

    auto valid = [](PrintBlockExtent extent) {
        return std::isfinite(extent.top) && std::isfinite(extent.bottom);
    };
    auto cursor = std::size_t{0};
    while (cursor < remaining.size()
        && (!valid(remaining[cursor]) || remaining[cursor].bottom <= document_top + epsilon))
        ++cursor;

    if (cursor >= remaining.size()) {
        auto bottom = (std::min)(document_top + page_extent, document_bottom);
        if (bottom <= document_top + epsilon) bottom = document_top + page_extent;
        return {{document_top, bottom}, remaining.size(), bottom, false};
    }

    auto first = cursor;
    auto limit = document_top + page_extent;
    auto page_bottom = document_top;
    auto fitted = false;
    while (cursor < remaining.size()) {
        auto extent = remaining[cursor];
        if (!valid(extent)) { ++cursor; continue; }
        extent.top = (std::max)(document_top, extent.top);
        extent.bottom = (std::max)(extent.top, extent.bottom);
        if (extent.bottom > limit + epsilon) break;
        page_bottom = (std::max)(page_bottom, extent.bottom);
        fitted = true;
        ++cursor;
    }

    if (fitted) {
        auto has_more = cursor < remaining.size();
        auto next_top = page_bottom;
        if (has_more && valid(remaining[cursor]))
            next_top = (std::max)(page_bottom, remaining[cursor].top);
        return {{document_top, page_bottom}, cursor, next_top, has_more};
    }

    // No complete block fits. Advance within the first oversized block, but
    // consume only invalid/finished leading entries so the same block remains
    // available to the following page when it still crosses the boundary.
    auto first_extent = remaining[first];
    first_extent.top = (std::max)(document_top, first_extent.top);
    first_extent.bottom = (std::max)(first_extent.top, first_extent.bottom);
    page_bottom = (std::min)(limit, first_extent.bottom);
    if (page_bottom <= document_top + epsilon) page_bottom = limit;

    auto consumed = first;
    if (page_bottom >= first_extent.bottom - epsilon) consumed = first + 1;
    auto has_more = consumed < remaining.size();
    auto next_top = page_bottom;
    if (has_more && consumed > first && valid(remaining[consumed]))
        next_top = (std::max)(page_bottom, remaining[consumed].top);
    return {{document_top, page_bottom}, consumed, next_top, has_more};
}

// Plan pages in document coordinates. Blocks that fit are never split; an
// individual block taller than a page is the only case where a page boundary
// may fall inside a block. Inter-block gaps are not carried to the top of the
// next page.
inline std::vector<PrintPageSlice> plan_print_pages(
    std::span<PrintBlockExtent const> input,
    float document_top,
    float document_bottom,
    float page_extent)
{
    constexpr float epsilon = 0.01f;
    page_extent = std::isfinite(page_extent) ? (std::max)(1.0f, page_extent) : 1.0f;
    document_top = std::isfinite(document_top) ? document_top : 0.0f;
    document_bottom = std::isfinite(document_bottom)
        ? (std::max)(document_top, document_bottom)
        : document_top;

    std::vector<PrintBlockExtent> blocks;
    blocks.reserve(input.size());
    for (auto extent : input) {
        if (!std::isfinite(extent.top) || !std::isfinite(extent.bottom)) continue;
        extent.top = (std::max)(document_top, extent.top);
        extent.bottom = (std::max)(extent.top, extent.bottom);
        if (extent.bottom <= document_top + epsilon) continue;
        if (!blocks.empty() && extent.top < blocks.back().bottom) {
            extent.top = blocks.back().bottom;
            extent.bottom = (std::max)(extent.top, extent.bottom);
        }
        blocks.push_back(extent);
    }

    if (blocks.empty()) {
        return {{document_top, (std::min)(document_top + page_extent, document_bottom)}};
    }

    std::vector<PrintPageSlice> pages;
    std::size_t index = 0;
    auto page_top = document_top;
    while (index < blocks.size()) {
        while (index < blocks.size() && blocks[index].bottom <= page_top + epsilon) ++index;
        if (index >= blocks.size()) break;

        auto first = index;
        auto limit = page_top + page_extent;
        auto page_bottom = page_top;
        while (index < blocks.size() && blocks[index].bottom <= limit + epsilon) {
            page_bottom = (std::max)(page_bottom, blocks[index].bottom);
            ++index;
        }

        if (index != first) {
            pages.push_back({page_top, page_bottom});
            if (index < blocks.size()) page_top = (std::max)(page_bottom, blocks[index].top);
            continue;
        }

        // The first remaining block crosses the page limit. Preserve progress
        // even for malformed/zero-height input and continue within that block.
        page_bottom = (std::min)(limit, blocks[index].bottom);
        if (page_bottom <= page_top + epsilon) page_bottom = page_top + page_extent;
        pages.push_back({page_top, page_bottom});
        page_top = page_bottom;
        if (page_top >= blocks[index].bottom - epsilon) {
            ++index;
            if (index < blocks.size()) page_top = (std::max)(page_top, blocks[index].top);
        }
    }

    if (pages.empty()) pages.push_back({document_top, document_bottom});
    return pages;
}

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

#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.ids;
import folia.core.layout_plan;

using namespace folia;
using namespace boost::ut;


suite layout_plan_tests = [] {

"layout_plan_assigns_every_block_before_painting"_test = [] {
    std::vector<BlockLayoutInput> inputs{
        {BlockId{1}, 20.0f, false, false},
        {BlockId{2}, 30.0f, false, false},
        {BlockId{3}, 10.0f, true, false},
        {BlockId{4}, 12.0f, true, false},
        {BlockId{5}, 40.0f, false, false},
    };
    LayoutPlanSettings settings;
    settings.document_top = 8.0f;
    settings.document_bottom_padding = 6.0f;
    settings.block_gap = 10.0f;
    settings.blank_gap = 5.0f;
    auto plan = plan_document_layout(inputs, settings);
    expect(fatal(bool((plan.blocks.size()) == (5u))));
    expect(fatal(bool((plan.blocks[0].top) == (8.0f))));
    expect(fatal(bool((plan.blocks[1].top) == (38.0f))));
    expect(fatal(bool((plan.blocks[2].top) == (73.0f))));
    expect(fatal(bool((plan.blocks[3].top) == (83.0f))));
    expect(fatal(bool((plan.blocks[4].top) == (100.0f))));
    expect(fatal(bool((plan.total_height) == (146.0f))));
};

"layout_plan_separates_measure_and_embedded_windows"_test = [] {
    std::vector<BlockLayoutInput> inputs;
    for (std::uint64_t index = 0; index < 10; ++index) {
        inputs.push_back(BlockLayoutInput{BlockId{index + 1}, 100.0f, false, index == 0});
    }
    LayoutPlanSettings settings;
    settings.viewport_top = 400.0f;
    settings.viewport_height = 100.0f;
    settings.measure_overscan = 1.0f;
    settings.embedded_overscan = 2.0f;
    auto plan = plan_document_layout(inputs, settings);
    expect(fatal(bool(plan.blocks[0].measure)));
    expect(fatal(bool(!plan.blocks[1].measure)));
    expect(fatal(bool(plan.blocks[2].measure)));
    expect(fatal(bool(plan.blocks[6].measure)));
    expect(fatal(bool(!plan.blocks[7].measure)));
    expect(fatal(bool(plan.blocks[1].request_embedded)));
    expect(fatal(bool(plan.blocks[7].request_embedded)));
    expect(fatal(bool(!plan.blocks[8].request_embedded)));
};

"layout_plan_sanitizes_invalid_measurements"_test = [] {
    std::vector<BlockLayoutInput> inputs{
        {BlockId{1}, std::numeric_limits<float>::quiet_NaN(), false, false},
        {BlockId{2}, -10.0f, false, false},
        {BlockId{3}, 20.0f, false, false},
    };
    LayoutPlanSettings settings;
    settings.document_top = std::numeric_limits<float>::infinity();
    settings.document_bottom_padding = -4.0f;
    auto plan = plan_document_layout(inputs, settings);
    expect(fatal(bool((plan.blocks[0].height) == (0.0f))));
    expect(fatal(bool((plan.blocks[1].height) == (0.0f))));
    expect(fatal(bool((plan.blocks[2].top) == (0.0f))));
    expect(fatal(bool((plan.total_height) == (20.0f))));
};

"print_pages_keep_fitting_blocks_intact"_test = [] {
    std::vector<PrintBlockExtent> blocks{
        {40.0f, 180.0f},
        {200.0f, 460.0f},
        {490.0f, 760.0f},
        {800.0f, 1040.0f},
    };
    auto pages = plan_print_pages(blocks, 0.0f, 1080.0f, 700.0f);
    expect(fatal(bool((pages.size()) == (2u))));
    expect(fatal(bool((pages[0].source_top) == (0.0f))));
    expect(fatal(bool((pages[0].source_bottom) == (460.0f))));
    expect(fatal(bool((pages[1].source_top) == (490.0f))));
    expect(fatal(bool((pages[1].source_bottom) == (1040.0f))));
};

"print_pages_split_only_an_oversized_block"_test = [] {
    std::vector<PrintBlockExtent> blocks{{20.0f, 1620.0f}, {1640.0f, 1700.0f}};
    auto pages = plan_print_pages(blocks, 0.0f, 1720.0f, 700.0f);
    expect(fatal(bool((pages.size()) == (3u))));
    expect(fatal(bool((pages[0].source_bottom) == (700.0f))));
    expect(fatal(bool((pages[1].source_top) == (700.0f))));
    expect(fatal(bool((pages[1].source_bottom) == (1400.0f))));
    expect(fatal(bool((pages[2].source_top) == (1400.0f))));
    expect(fatal(bool((pages[2].source_bottom) == (1700.0f))));
};

"print_pages_handle_empty_and_invalid_layouts"_test = [] {
    std::vector<PrintBlockExtent> empty;
    auto emptyPages = plan_print_pages(empty, 0.0f, 120.0f, 700.0f);
    expect(fatal(bool((emptyPages.size()) == (1u))));
    expect(fatal(bool((emptyPages[0].source_bottom) == (120.0f))));

    std::vector<PrintBlockExtent> invalid{
        {std::numeric_limits<float>::quiet_NaN(), 4.0f},
        {10.0f, 5.0f},
    };
    auto pages = plan_print_pages(invalid, 0.0f, 20.0f, 0.0f);
    expect(fatal(bool(!pages.empty())));
    for (auto const& page : pages) expect(fatal(bool(page.source_bottom > page.source_top)));
};

"next_print_page_streams_the_same_block_boundaries"_test = [] {
    std::vector<PrintBlockExtent> blocks{
        {40.0f, 180.0f},
        {200.0f, 460.0f},
        {490.0f, 760.0f},
        {800.0f, 1040.0f},
    };
    auto first = plan_next_print_page(blocks, 0.0f, 1080.0f, 700.0f);
    expect(fatal(bool((first.slice.source_bottom) == (460.0f))));
    expect(fatal(bool((first.consumed_blocks) == (2u))));
    expect(fatal(bool((first.next_source_top) == (490.0f))));
    expect(fatal(bool(first.has_more)));

    auto second = plan_next_print_page(
        std::span<PrintBlockExtent const>{blocks}.subspan(first.consumed_blocks),
        first.next_source_top,
        1080.0f,
        700.0f);
    expect(fatal(bool((second.slice.source_top) == (490.0f))));
    expect(fatal(bool((second.slice.source_bottom) == (1040.0f))));
    expect(fatal(bool((second.consumed_blocks) == (2u))));
    expect(fatal(bool(!second.has_more)));
};

"next_print_page_keeps_an_oversized_block_as_the_working_suffix"_test = [] {
    std::vector<PrintBlockExtent> blocks{{20.0f, 1620.0f}, {1640.0f, 1700.0f}};
    auto first = plan_next_print_page(blocks, 0.0f, 1720.0f, 700.0f);
    expect(fatal(bool((first.slice.source_bottom) == (700.0f))));
    expect(fatal(bool((first.consumed_blocks) == (0u))));

    auto second = plan_next_print_page(blocks, first.next_source_top, 1720.0f, 700.0f);
    expect(fatal(bool((second.slice.source_bottom) == (1400.0f))));
    expect(fatal(bool((second.consumed_blocks) == (0u))));

    auto third = plan_next_print_page(blocks, second.next_source_top, 1720.0f, 700.0f);
    expect(fatal(bool((third.slice.source_bottom) == (1700.0f))));
    expect(fatal(bool((third.consumed_blocks) == (2u))));
    expect(fatal(bool(!third.has_more)));
};

"streamed_print_pages_match_the_full_plan_for_varied_documents"_test = [] {
    std::mt19937 random{0xE1D0C23u};
    std::uniform_int_distribution<int> blockCount{1, 80};
    std::uniform_real_distribution<float> gap{0.0f, 36.0f};
    std::uniform_real_distribution<float> height{8.0f, 1100.0f};
    for (auto sample = 0; sample < 200; ++sample) {
        std::vector<PrintBlockExtent> blocks;
        auto cursor = 0.0f;
        for (auto index = 0; index < blockCount(random); ++index) {
            cursor += gap(random);
            auto bottom = cursor + height(random);
            blocks.push_back({cursor, bottom});
            cursor = bottom;
        }
        auto documentBottom = cursor + gap(random);
        auto complete = plan_print_pages(blocks, 0.0f, documentBottom, 700.0f);

        std::vector<PrintPageSlice> streamed;
        auto consumed = std::size_t{0};
        auto sourceTop = 0.0f;
        for (auto page = std::size_t{0}; page <= complete.size(); ++page) {
            auto step = plan_next_print_page(
                std::span<PrintBlockExtent const>{blocks}.subspan(consumed),
                sourceTop,
                documentBottom,
                700.0f);
            streamed.push_back(step.slice);
            consumed += step.consumed_blocks;
            sourceTop = step.next_source_top;
            if (!step.has_more) break;
        }

        expect(fatal(bool((streamed.size()) == (complete.size()))));
        for (std::size_t page = 0; page < complete.size(); ++page) {
            expect(fatal(bool(std::abs(streamed[page].source_top - complete[page].source_top) < 0.01f)));
            expect(fatal(bool(std::abs(streamed[page].source_bottom - complete[page].source_bottom) < 0.01f)));
        }
    }
};

}; // suite layout_plan_tests

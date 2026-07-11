import std;
#include "test_framework.h"
import elmd.core.ids;
import elmd.core.layout_plan;

using namespace elmd;

ELMD_TEST(layout_plan_assigns_every_block_before_painting) {
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
    ELMD_CHECK_EQ(plan.blocks.size(), 5u);
    ELMD_CHECK_EQ(plan.blocks[0].top, 8.0f);
    ELMD_CHECK_EQ(plan.blocks[1].top, 38.0f);
    ELMD_CHECK_EQ(plan.blocks[2].top, 73.0f);
    ELMD_CHECK_EQ(plan.blocks[3].top, 83.0f);
    ELMD_CHECK_EQ(plan.blocks[4].top, 100.0f);
    ELMD_CHECK_EQ(plan.total_height, 146.0f);
}

ELMD_TEST(layout_plan_separates_measure_and_embedded_windows) {
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
    ELMD_CHECK(plan.blocks[0].measure);
    ELMD_CHECK(!plan.blocks[1].measure);
    ELMD_CHECK(plan.blocks[2].measure);
    ELMD_CHECK(plan.blocks[6].measure);
    ELMD_CHECK(!plan.blocks[7].measure);
    ELMD_CHECK(plan.blocks[1].request_embedded);
    ELMD_CHECK(plan.blocks[7].request_embedded);
    ELMD_CHECK(!plan.blocks[8].request_embedded);
}

ELMD_TEST(layout_plan_sanitizes_invalid_measurements) {
    std::vector<BlockLayoutInput> inputs{
        {BlockId{1}, std::numeric_limits<float>::quiet_NaN(), false, false},
        {BlockId{2}, -10.0f, false, false},
        {BlockId{3}, 20.0f, false, false},
    };
    LayoutPlanSettings settings;
    settings.document_top = std::numeric_limits<float>::infinity();
    settings.document_bottom_padding = -4.0f;
    auto plan = plan_document_layout(inputs, settings);
    ELMD_CHECK_EQ(plan.blocks[0].height, 0.0f);
    ELMD_CHECK_EQ(plan.blocks[1].height, 0.0f);
    ELMD_CHECK_EQ(plan.blocks[2].top, 0.0f);
    ELMD_CHECK_EQ(plan.total_height, 20.0f);
}

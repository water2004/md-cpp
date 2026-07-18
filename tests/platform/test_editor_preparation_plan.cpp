#include "support/folia_test.hpp"

import folia.core.text_edit;
import folia.platform.editor_preparation_plan;

using namespace boost::ut;
using namespace folia::platform::editor;

namespace
{
    folia::TextPosition Position(std::uint64_t owner, std::size_t offset = 0)
    {
        return {folia::NodeId{owner}, offset, folia::TextAffinity::Downstream};
    }

    EditorPreparationCacheView Cache(
        std::size_t count = 8,
        std::uint64_t revision = 10,
        float width = 640.0f,
        std::uint64_t theme = 2,
        folia::TextPosition active = Position(1))
    {
        return {
            .available = true,
            .blockCount = count,
            .modelRevision = revision,
            .documentWidth = width,
            .themeRevision = theme,
            .active = active,
        };
    }
}

suite editor_preparation_plan_tests = [] {

"missing cache and structural key changes require a complete rebuild"_test = [] {
    auto missing = BuildEditorPreparationInvalidationPlan(
        {}, 8, 10, 640.0f, 2, Position(1), false, false, {}, {}, {});
    expect(missing.rebuildAll);

    auto widthChanged = BuildEditorPreparationInvalidationPlan(
        Cache(), 8, 10, 641.0f, 2, Position(1), false, false, {}, {}, {});
    expect(widthChanged.rebuildAll);

    auto nonIncrementalModel = BuildEditorPreparationInvalidationPlan(
        Cache(), 8, 11, 640.0f, 2, Position(1), false, false, {}, {}, {});
    expect(nonIncrementalModel.rebuildAll);
    expect(nonIncrementalModel.modelChanged);
};

"incremental model updates normalize changed block indices"_test = [] {
    std::array<std::size_t, 5> changed{5, 2, 5, 99, 3};
    auto plan = BuildEditorPreparationInvalidationPlan(
        Cache(),
        8,
        11,
        640.0f,
        2,
        Position(1),
        true,
        false,
        changed,
        {},
        {});
    expect(!plan.rebuildAll);
    expect(plan.modelChanged);
    expect(plan.invalidatedBlocks == std::vector<std::size_t>{2, 3, 5});
};

"rendered caret moves invalidate only the previous and active owner blocks"_test = [] {
    auto plan = BuildEditorPreparationInvalidationPlan(
        Cache(),
        8,
        10,
        640.0f,
        2,
        Position(7, 4),
        true,
        false,
        {},
        4,
        1);
    expect(!plan.rebuildAll);
    expect(plan.activePositionChanged);
    expect(plan.invalidatedBlocks == std::vector<std::size_t>{1, 4});

    auto sameBlock = BuildEditorPreparationInvalidationPlan(
        Cache(),
        8,
        10,
        640.0f,
        2,
        Position(7, 4),
        true,
        false,
        {},
        4,
        4);
    expect(sameBlock.invalidatedBlocks == std::vector<std::size_t>{4});
};

"source mode caret moves do not invalidate text layouts"_test = [] {
    auto plan = BuildEditorPreparationInvalidationPlan(
        Cache(),
        8,
        10,
        640.0f,
        2,
        Position(7, 4),
        true,
        true,
        {},
        4,
        1);
    expect(plan.activePositionChanged);
    expect(plan.invalidatedBlocks.empty());
};

"stale changed-block hints are ignored without a model revision change"_test = [] {
    std::array<std::size_t, 2> changed{2, 3};
    auto plan = BuildEditorPreparationInvalidationPlan(
        Cache(),
        8,
        10,
        640.0f,
        2,
        Position(1),
        true,
        false,
        changed,
        {},
        {});
    expect(!plan.modelChanged);
    expect(plan.invalidatedBlocks.empty());
};

}; // suite editor_preparation_plan_tests

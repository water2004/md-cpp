#include "support/folia_test.hpp"

import elmd.platform.editor_selection_drag_model;

using namespace boost::ut;
using namespace elmd::platform::editor;

suite editor_selection_drag_model_tests = [] {

"selection pointer inside viewport does not auto scroll"_test = [] {
    auto sample = ProjectEditorSelectionPointer(40.0f, 60.0f, 200.0f, 100.0f);
    expect(sample.hitX == 40.0f);
    expect(sample.hitY == 60.0f);
    expect(sample.autoScrollVelocity == 0.0f);
};

"selection pointer outside viewport clamps hit testing and accelerates"_test = [] {
    auto nearBottom = ProjectEditorSelectionPointer(250.0f, 110.0f, 200.0f, 100.0f);
    auto farBottom = ProjectEditorSelectionPointer(250.0f, 180.0f, 200.0f, 100.0f);
    expect(nearBottom.hitX == 200.0f);
    expect(nearBottom.hitY == 99.5f);
    expect(nearBottom.autoScrollVelocity > 0.0f);
    expect(farBottom.autoScrollVelocity > nearBottom.autoScrollVelocity);

    auto above = ProjectEditorSelectionPointer(-20.0f, -30.0f, 200.0f, 100.0f);
    expect(above.hitX == 0.0f);
    expect(above.hitY == 0.0f);
    expect(above.autoScrollVelocity < 0.0f);
};

"selection auto scroll is bounded and invalid geometry stays finite"_test = [] {
    auto far = ProjectEditorSelectionPointer(0.0f, 100000.0f, 100.0f, 100.0f);
    expect(far.autoScrollVelocity == 2400.0f);

    auto invalid = ProjectEditorSelectionPointer(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -10.0f,
        std::numeric_limits<float>::quiet_NaN());
    expect(std::isfinite(invalid.hitX));
    expect(std::isfinite(invalid.hitY));
    expect(invalid.autoScrollVelocity == 0.0f);
};

}; // suite editor_selection_drag_model_tests

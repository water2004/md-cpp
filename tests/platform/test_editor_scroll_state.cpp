#include "support/folia_test.hpp"

import elmd.platform.editor_scroll_state;

using namespace boost::ut;
using elmd::platform::editor::EditorScrollState;

suite editor_scroll_state_tests = [] {

"queued scroll is monotonic and frame-rate independent"_test = [] {
    EditorScrollState sixtyHz;
    EditorScrollState oneTwentyHz;
    sixtyHz.Queue(480.0f, 2000.0f);
    oneTwentyHz.Queue(480.0f, 2000.0f);

    auto previous = sixtyHz.Offset();
    for (auto frame = 0; frame < 60; ++frame)
    {
        sixtyHz.Advance(1.0f / 60.0f);
        expect(sixtyHz.Offset() >= previous);
        expect(sixtyHz.Offset() <= sixtyHz.Target());
        previous = sixtyHz.Offset();
    }
    for (auto frame = 0; frame < 120; ++frame)
        oneTwentyHz.Advance(1.0f / 120.0f);

    expect(std::fabs(sixtyHz.Offset() - oneTwentyHz.Offset()) < 0.01f);
};

"direction reversal starts from the visible offset"_test = [] {
    EditorScrollState state;
    state.Set(500.0f, 2000.0f);
    state.Queue(300.0f, 2000.0f);
    state.Advance(1.0f / 60.0f);
    auto visible = state.Offset();
    state.Queue(-100.0f, 2000.0f);

    expect(state.Target() < visible);
    expect(std::fabs(state.Target() - (visible - 100.0f)) < 0.01f);
};

"extent changes and anchor shifts clamp offset and target together"_test = [] {
    EditorScrollState state;
    state.Set(900.0f, 1000.0f);
    state.Queue(100.0f, 1000.0f);
    state.Shift(80.0f, 1200.0f);
    expect(state.Offset() == 980.0_f);
    expect(state.Target() == 1080.0_f);

    state.Clamp(700.0f);
    expect(state.Offset() == 700.0_f);
    expect(state.Target() == 700.0_f);

    auto snapshot = state.Save();
    state.Set(50.0f, 700.0f);
    state.Restore(snapshot, 600.0f);
    expect(state.Offset() == 600.0_f);
    expect(state.Target() == 600.0_f);
};

}; // suite editor_scroll_state_tests

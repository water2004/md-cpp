#include "support/folia_test.hpp"

import elmd.platform.editor_geometry;
import elmd.platform.editor_viewport_plan;

using namespace boost::ut;
using namespace elmd::platform::editor;

namespace
{
    EditorBlockGeometryIndex UniformGeometry(std::size_t count, float height)
    {
        std::vector<EditorBlockGeometryIndex::Entry> entries(
            count,
            EditorBlockGeometryIndex::Entry{0.0f, height, 0.0f});
        EditorBlockGeometryIndex geometry;
        geometry.Reset(std::move(entries), 0.0f);
        return geometry;
    }
}

suite editor_viewport_plan_tests = [] {

"viewport plan separates visible and directional prefetch work"_test = [] {
    auto geometry = UniformGeometry(100, 100.0f);
    EditorViewportPolicy policy;
    policy.viewportOverscan = 0.0f;
    policy.minimumPrefetch = 200.0f;
    policy.prefetchViewportFactor = 0.0f;

    auto forward = BuildEditorViewportPlan(
        geometry,
        1000.0f,
        300.0f,
        false,
        true,
        policy);
    expect(forward.visible == EditorIndexRange{9, 14});
    expect(forward.prefetch == EditorIndexRange{14, 16});

    auto backward = BuildEditorViewportPlan(
        geometry,
        1000.0f,
        300.0f,
        false,
        false,
        policy);
    expect(backward.visible == forward.visible);
    expect(backward.prefetch == EditorIndexRange{7, 9});
};

"print viewport does not schedule speculative prefetch"_test = [] {
    auto geometry = UniformGeometry(20, 50.0f);
    auto plan = BuildEditorViewportPlan(
        geometry,
        200.0f,
        100.0f,
        true,
        true);
    expect(!plan.visible.Empty());
    expect(plan.prefetch.Empty());
    expect(plan.retention.begin >= plan.visible.begin);
    expect(plan.retention.end == plan.visible.end);
};

"viewport ranges remain safe for empty and invalid extents"_test = [] {
    EditorBlockGeometryIndex empty;
    empty.Reset({}, 12.0f);
    auto plan = BuildEditorViewportPlan(
        empty,
        std::numeric_limits<float>::infinity(),
        -100.0f,
        false,
        true);
    expect(plan.visible.Empty());
    expect(plan.prefetch.Empty());
    expect(plan.embedded.Empty());
    expect(plan.retention.Empty());
};

}; // suite editor_viewport_plan_tests

#include "support/folia_test.hpp"

import elmd.core.ids;
import elmd.core.selection;
import elmd.core.text_edit;
import elmd.platform.editor_display_mapping;

using namespace boost::ut;
using namespace elmd;
using namespace elmd::platform::editor;

suite display_mapping_tests = [] {

"downstream_empty_content_caret_follows_generated_prefix"_test = [] {
    const auto owner = NodeId{42};
    const EditorDisplayMapping mapping{
        {owner, 0, TextAffinity::Downstream, EditorDisplayPositionKind::BoundaryDecoration},
        {owner, 0, TextAffinity::Downstream, EditorDisplayPositionKind::BoundaryDecoration},
        {owner, 0, TextAffinity::Upstream, EditorDisplayPositionKind::BoundaryDecoration},
    };

    expect(DisplayPositionForSource(
        mapping,
        {owner, 0, TextAffinity::Downstream}) == 2u);
    expect(DisplayPositionForSource(
        mapping,
        {owner, 0, TextAffinity::Upstream}) == 2u);
};

"real_source_mapping_still_wins_after_generated_prefix"_test = [] {
    const auto owner = NodeId{43};
    const EditorDisplayMapping mapping{
        {owner, 0, TextAffinity::Downstream, EditorDisplayPositionKind::BoundaryDecoration},
        {owner, 0, TextAffinity::Downstream, EditorDisplayPositionKind::BoundaryDecoration},
        {owner, 0, TextAffinity::Downstream, EditorDisplayPositionKind::Source},
        {owner, 1, TextAffinity::Downstream, EditorDisplayPositionKind::Source},
    };

    expect(DisplayPositionForSource(
        mapping,
        {owner, 0, TextAffinity::Downstream}) == 2u);
};

"non_boundary_generated_overlays_keep_their_original_affinity_choice"_test = [] {
    const auto owner = NodeId{44};
    const EditorDisplayMapping mapping{
        {owner, 0, TextAffinity::Downstream, EditorDisplayPositionKind::Generated},
        {owner, 0, TextAffinity::Downstream, EditorDisplayPositionKind::Generated},
        {owner, 1, TextAffinity::Downstream, EditorDisplayPositionKind::Source},
    };

    expect(DisplayPositionForSource(
        mapping,
        {owner, 0, TextAffinity::Downstream}) == 0u);
};

}; // suite display_mapping_tests

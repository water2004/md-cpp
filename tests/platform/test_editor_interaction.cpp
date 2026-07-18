#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "support/folia_test.hpp"

import elmd.core.ids;
import elmd.core.selection;
import elmd.core.text_edit;
import elmd.platform.dwrite_factory;
import elmd.platform.editor_display_mapping;
import elmd.platform.editor_interaction;

using namespace boost::ut;
using namespace elmd;
using namespace elmd::platform;
using namespace elmd::platform::editor;

namespace
{
    EditorDisplayMapping IdentityMapping(NodeId owner, std::size_t length)
    {
        EditorDisplayMapping mapping;
        mapping.reserve(length + 1);
        for (std::size_t offset = 0; offset <= length; ++offset)
        {
            mapping.emplace_back(
                owner,
                offset,
                TextAffinity::Downstream,
                EditorDisplayPositionKind::Source);
        }
        return mapping;
    }

    Microsoft::WRL::ComPtr<IDWriteTextLayout> MakeLayout(
        std::wstring_view text,
        float width = 400.0f)
    {
        auto factory = create_dwrite_factory();
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        auto formatResult = factory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            18.0f,
            L"en-US",
            &format);
        expect(SUCCEEDED(formatResult));

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        auto layoutResult = factory->CreateTextLayout(
            text.data(),
            static_cast<UINT32>(text.size()),
            format.Get(),
            width,
            200.0f,
            &layout);
        expect(SUCCEEDED(layoutResult));
        return layout;
    }
}

suite editor_interaction_tests = [] {

"directwrite block geometry keeps source coordinates authoritative"_test = [] {
    constexpr auto length = std::size_t{10};
    const auto owner = NodeId{501};
    EditorInteractionMap interaction;
    interaction.blocks.push_back(EditorVisualBlock{
        .rect = D2D1_RECT_F{10.0f, 20.0f, 410.0f, 80.0f},
        .textOrigin = D2D1_POINT_2F{10.0f, 20.0f},
        .textWidth = 400.0f,
        .sourceSpan = {owner, {0, length}},
        .documentY = 20.0f,
        .text = U"alpha beta",
        .displayToSource = IdentityMapping(owner, length),
        .layout = MakeLayout(L"alpha beta"),
    });

    interaction.AddBlockLines(0);
    expect(interaction.lines.size() == 1_u);
    expect(interaction.lines.front().sourceSpans.size() == 1_u);
    expect(interaction.lines.front().sourceSpans.front() == TextSpan{owner, {0, length}});

    auto start = interaction.CaretBounds({owner, 0, TextAffinity::Downstream}, 24.0f);
    auto middle = interaction.CaretBounds({owner, 5, TextAffinity::Downstream}, 24.0f);
    auto end = interaction.CaretBounds({owner, length, TextAffinity::Upstream}, 24.0f);
    expect(start.has_value());
    expect(middle.has_value());
    expect(end.has_value());
    expect(start->left < middle->left);
    expect(middle->left < end->left);

    auto hit = interaction.HitTest(middle->left, (middle->top + middle->bottom) * 0.5f);
    expect(hit.has_value());
    expect(hit->container_id == owner);
    expect(hit->source_offset == 5_u);

    expect(interaction.VisualLineStart({owner, 5, TextAffinity::Downstream})
        == TextPosition{owner, 0, TextAffinity::Downstream});
    expect(interaction.VisualLineEnd({owner, 5, TextAffinity::Downstream})
        == TextPosition{owner, length, TextAffinity::Upstream});
};

"directwrite visual movement crosses explicit lines without serialized offsets"_test = [] {
    constexpr auto length = std::size_t{10};
    const auto owner = NodeId{502};
    EditorInteractionMap interaction;
    interaction.blocks.push_back(EditorVisualBlock{
        .rect = D2D1_RECT_F{30.0f, 40.0f, 330.0f, 120.0f},
        .textOrigin = D2D1_POINT_2F{30.0f, 40.0f},
        .textWidth = 300.0f,
        .sourceSpan = {owner, {0, length}},
        .documentY = 40.0f,
        .text = U"alpha\nbeta",
        .displayToSource = IdentityMapping(owner, length),
        .layout = MakeLayout(L"alpha\nbeta", 300.0f),
    });

    interaction.AddBlockLines(0);
    expect(interaction.lines.size() == 2_u);

    float goalX = -1.0f;
    auto moved = interaction.MoveCaretVertically(
        {owner, 2, TextAffinity::Downstream},
        true,
        goalX,
        24.0f);
    expect(moved.has_value());
    expect(moved->container_id == owner);
    expect(moved->source_offset >= 6_u);
    expect(moved->source_offset <= length);
    expect(goalX >= 30.0f);
};

}; // suite editor_interaction_tests

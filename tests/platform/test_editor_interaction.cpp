#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "support/folia_test.hpp"

import folia.core.ids;
import folia.core.selection;
import folia.core.text_edit;
import folia.platform.dwrite_factory;
import folia.platform.editor_display_mapping;
import folia.platform.editor_interaction;

using namespace boost::ut;
using namespace folia;
using namespace folia::platform;
using namespace folia::platform::editor;

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

    EditorVisualTable MakeEditableTable(NodeId owner)
    {
        EditorVisualTable table;
        table.rect = D2D1_RECT_F{0.0f, 0.0f, 200.0f, 100.0f};
        table.sourceSpans = {{owner, {0, 40}}};
        table.rowCount = 2;
        table.columnCount = 2;
        table.rowBoundaries = {0.0f, 50.0f, 100.0f};
        table.columnBoundaries = {0.0f, 100.0f, 200.0f};
        for (std::size_t row = 0; row < table.rowCount; ++row)
        {
            for (std::size_t column = 0; column < table.columnCount; ++column)
            {
                auto offset = (row * table.columnCount + column) * 10;
                table.cells.push_back(EditorVisualTableCell{
                    .sourceSpan = {owner, {offset, offset + 10}},
                    .row = row,
                    .column = column,
                });
            }
        }
        return table;
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

"table hit policy returns semantic actions without renderer ownership"_test = [] {
    const auto owner = NodeId{503};
    EditorInteractionMap interaction;
    interaction.tables.push_back(MakeEditableTable(owner));

    auto insertRow = interaction.TableActionAt(50.0f, 0.0f);
    expect(insertRow.has_value());
    expect(insertRow->kind == EditorTableActionKind::InsertRow);
    expect(insertRow->index == 0_u);
    expect(insertRow->sourcePosition == TextPosition{owner, 0, TextAffinity::Downstream});

    auto insertColumn = interaction.TableActionAt(0.0f, 25.0f);
    expect(insertColumn.has_value());
    expect(insertColumn->kind == EditorTableActionKind::InsertColumn);
    expect(insertColumn->index == 0_u);

    auto dragRow = interaction.TableActionAt(-40.0f, 25.0f);
    expect(dragRow.has_value());
    expect(dragRow->kind == EditorTableActionKind::DragRow);
    expect(dragRow->index == 0_u);

    auto deleteRow = interaction.TableActionAt(-15.0f, 75.0f);
    expect(deleteRow.has_value());
    expect(deleteRow->kind == EditorTableActionKind::DeleteRow);
    expect(deleteRow->index == 1_u);

    auto dragColumn = interaction.TableActionAt(40.0f, -18.0f);
    expect(dragColumn.has_value());
    expect(dragColumn->kind == EditorTableActionKind::DragColumn);
    expect(dragColumn->index == 0_u);

    auto deleteColumn = interaction.TableActionAt(60.0f, -18.0f);
    expect(deleteColumn.has_value());
    expect(deleteColumn->kind == EditorTableActionKind::DeleteColumn);
    expect(deleteColumn->index == 0_u);
};

"table drop policy stays within the source table and tolerates incomplete geometry"_test = [] {
    const auto owner = NodeId{504};
    EditorInteractionMap interaction;
    interaction.tables.push_back(MakeEditableTable(owner));

    EditorTableAction dragged{
        EditorTableActionKind::DragRow,
        TextPosition{owner, 0, TextAffinity::Downstream},
        0,
    };
    expect(interaction.TableDropIndexAt(dragged, 50.0f, 80.0f, true) == std::optional<std::size_t>{2});
    expect(interaction.TableDropIndexAt(dragged, 130.0f, 25.0f, false) == std::optional<std::size_t>{1});

    auto foreign = dragged;
    foreign.sourcePosition.container_id = NodeId{999};
    expect(!interaction.TableDropIndexAt(foreign, 50.0f, 80.0f, true).has_value());

    EditorVisualTable incomplete;
    incomplete.editable = true;
    incomplete.rowCount = 1;
    incomplete.columnCount = 1;
    interaction.tables = {std::move(incomplete)};
    expect(!interaction.TableActionAt(0.0f, 0.0f).has_value());
    expect(!interaction.TableDropIndexAt(std::nullopt, 0.0f, 0.0f, true).has_value());
};

}; // suite editor_interaction_tests

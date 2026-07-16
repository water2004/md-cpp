#include "../src/app-winui/editor/rendering/EditorBlockGeometryIndex.h"
#include "elmd_test.hpp"

using namespace boost::ut;

namespace
{
    using Geometry = winrt::ElMd::EditorBlockGeometryIndex;

    Geometry::Placement NaivePlacement(
        std::vector<Geometry::Entry> const& entries,
        float padding,
        std::size_t index)
    {
        auto cursor = padding;
        for (std::size_t current = 0; current < entries.size(); ++current)
        {
            auto const& entry = entries[current];
            auto top = cursor + entry.marginTop;
            auto bottom = top + entry.height;
            if (current == index) return {top, bottom};
            cursor = bottom + entry.trailing;
        }
        return {};
    }

    float NaiveTotal(std::vector<Geometry::Entry> const& entries, float padding)
    {
        auto cursor = padding;
        for (auto const& entry : entries)
            cursor += entry.marginTop + entry.height + entry.trailing;
        return cursor + padding;
    }

    std::size_t NaiveFirstIntersecting(
        std::vector<Geometry::Entry> const& entries,
        float padding,
        float documentTop)
    {
        for (std::size_t index = 0; index < entries.size(); ++index)
            if (NaivePlacement(entries, padding, index).bottom >= documentTop) return index;
        return entries.size();
    }
}

suite block_geometry_tests = [] {

"block geometry preserves margins gaps and inclusive intersections"_test = [] {
    std::vector<Geometry::Entry> entries{
        {2.0f, 10.0f, 3.0f},
        {4.0f, 20.0f, 5.0f},
        {0.0f, 7.0f, 1.0f},
    };
    Geometry geometry;
    geometry.Reset(entries, 8.0f);

    expect(geometry.Initialized());
    expect(geometry.Size() == 3_u);
    expect(geometry.At(0).top == 10.0_f);
    expect(geometry.At(0).bottom == 20.0_f);
    expect(geometry.At(1).top == 27.0_f);
    expect(geometry.At(1).bottom == 47.0_f);
    expect(geometry.At(2).top == 52.0_f);
    expect(geometry.At(2).bottom == 59.0_f);
    expect(geometry.TotalHeight() == 68.0_f);
    expect(geometry.FirstIntersecting(20.0f) == 0_u);
    expect(geometry.FirstIntersecting(20.5f) == 1_u);
    expect(geometry.FirstIntersecting(48.0f) == 2_u);
    expect(geometry.FirstIntersecting(60.0f) == 3_u);
};

"block geometry updates height without moving preceding blocks"_test = [] {
    std::vector<Geometry::Entry> entries{
        {2.0f, 10.0f, 3.0f},
        {4.0f, 20.0f, 5.0f},
        {0.0f, 7.0f, 1.0f},
    };
    Geometry geometry;
    geometry.Reset(entries, 8.0f);
    geometry.UpdateHeight(1, 32.0f);

    expect(geometry.At(0).top == 10.0_f);
    expect(geometry.At(1).top == 27.0_f);
    expect(geometry.At(1).bottom == 59.0_f);
    expect(geometry.At(2).top == 64.0_f);
    expect(geometry.TotalHeight() == 80.0_f);
};

"block geometry retains local margin changes with equal total extent"_test = [] {
    std::vector<Geometry::Entry> entries{
        {2.0f, 10.0f, 3.0f},
        {1.0f, 8.0f, 2.0f},
    };
    Geometry geometry;
    geometry.Reset(entries, 5.0f);
    auto secondTop = geometry.At(1).top;
    auto total = geometry.TotalHeight();

    geometry.Update(0, {4.0f, 10.0f, 1.0f});

    expect(geometry.At(0).top == 9.0_f);
    expect(geometry.At(0).bottom == 19.0_f);
    expect(geometry.At(1).top == secondTop);
    expect(geometry.TotalHeight() == total);
};

"block geometry matches a naive layout under random updates"_test = [] {
    constexpr float padding = 12.0f;
    std::uint32_t state = 0x9e3779b9u;
    auto next = [&]
    {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    std::vector<Geometry::Entry> entries;
    entries.reserve(257);
    for (std::size_t index = 0; index < 257; ++index)
    {
        entries.push_back({
            static_cast<float>(next() % 9u),
            static_cast<float>(1u + next() % 80u),
            static_cast<float>(next() % 11u),
        });
    }
    Geometry geometry;
    geometry.Reset(entries, padding);

    for (std::size_t operation = 0; operation < 512; ++operation)
    {
        auto index = static_cast<std::size_t>(next()) % entries.size();
        auto updated = Geometry::Entry{
            static_cast<float>(next() % 9u),
            static_cast<float>(1u + next() % 80u),
            static_cast<float>(next() % 11u),
        };
        entries[index] = updated;
        geometry.Update(index, updated);

        auto probe = static_cast<std::size_t>(next()) % entries.size();
        auto expected = NaivePlacement(entries, padding, probe);
        auto actual = geometry.At(probe);
        expect(actual.top == expected.top);
        expect(actual.bottom == expected.bottom);
        expect(geometry.TotalHeight() == NaiveTotal(entries, padding));

        auto documentTop = static_cast<float>(next() % 30000u);
        expect(geometry.FirstIntersecting(documentTop)
            == NaiveFirstIntersecting(entries, padding, documentTop));
    }
};

"empty block geometry keeps document padding and safe bounds"_test = [] {
    Geometry geometry;
    geometry.Reset({}, 9.0f);
    geometry.UpdateHeight(4, 20.0f);
    geometry.Update(3, {1.0f, 2.0f, 3.0f});
    expect(geometry.Size() == 0_u);
    expect(geometry.TotalHeight() == 18.0_f);
    expect(geometry.FirstIntersecting(0.0f) == 0_u);
    expect(geometry.At(0).top == 0.0_f);
    expect(geometry.At(0).bottom == 0.0_f);
};

};

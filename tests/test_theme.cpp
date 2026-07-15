#include "elmd_test.hpp"
import elmd.core.theme;
import elmd.core.parser;
import elmd.core.render_builder;

using namespace elmd;
using namespace boost::ut;

suite theme_tests = [] {

"theme profiles carry typography color and layout"_test = [] {
    auto const& dark = default_theme_profile(Theme::Dark);
    auto const& light = default_theme_profile(Theme::Light);

    expect(dark.schema_version == 1_u);
    expect(!dark.id.empty());
    expect(dark.typography.body.size > 0.0_f);
    expect(dark.typography.code.family != dark.typography.body.family);
    expect(dark.colors.bg != light.colors.bg);
    expect(dark.colors.syntax.size() == 11_u);
    expect(dark.layout.document_horizontal_padding > 0.0_f);
};

"render builder consumes theme block layout"_test = [] {
    auto parsed = parse_text(1, "# heading\n\nparagraph\n\n```cpp\ncode\n```\n");
    auto custom = default_theme_profile(Theme::Dark);
    custom.layout.heading_margin_top[0] = 71.0f;
    custom.layout.paragraph_margin_bottom = 19.0f;
    custom.layout.code_padding_horizontal = 23.0f;

    auto model = build_render_model(parsed.document, parsed.outline, custom);
    expect(model.blocks.size() == 3_u);
    expect(model.blocks[0].block_style.margin_top == 71.0_f);
    expect(model.blocks[1].block_style.margin_bottom == 19.0_f);
    expect(model.blocks[2].block_style.padding_left == 23.0_f);
    expect(model.blocks[2].block_style.padding_right == 23.0_f);
};

"math styles consume the selected theme"_test = [] {
    auto custom = default_theme_profile(Theme::Light);
    custom.typography.body.size = 21.0f;
    custom.colors.math_fg = Color(1, 2, 3, 4);
    custom.colors.math_bg = Color(5, 6, 7, 8);

    auto style = MathStyle::block_default(custom);
    expect(style.font_size == 21.0_f);
    expect(style.display == MathDisplayMode::Block);
    expect(style.color == Color(1, 2, 3, 4));
    expect(style.background == Color(5, 6, 7, 8));
};

};

#include <cmath>
#include <string>
#include <unordered_map>

#include "elmd_test.hpp"
import elmd.core.html_inline_presentation;
import elmd.core.render_model;
import elmd.core.theme;

using namespace elmd;
using namespace boost::ut;

suite html_inline_presentation_tests = [] {

"semantic_html_tags_project_without_markdown_nodes"_test = [] {
    const std::unordered_map<std::string, std::string> empty;
    auto style = apply_html_inline_presentation(InlineStyle::plain(), "strong", empty);
    expect(style.bold);
    style = apply_html_inline_presentation(style, "em", empty);
    expect(style.bold);
    expect(style.italic);
    style = apply_html_inline_presentation(style, "u", empty);
    expect(style.underline);
    style = apply_html_inline_presentation(style, "del", empty);
    expect(style.strikethrough);
    style = apply_html_inline_presentation(style, "kbd", empty);
    expect(style.code);
};

"safe_inline_css_is_parsed_and_nested"_test = [] {
    const std::unordered_map<std::string, std::string> outer_attributes{{
        "style",
        "color: #1234; background-color: rgb(10, 20, 30); "
        "font-family: 'Segoe UI', sans-serif; font-size: 150%; "
        "font-weight: 650; font-style: italic; text-decoration: underline line-through"}};
    auto style = apply_html_inline_presentation(InlineStyle::plain(), "span", outer_attributes);
    expect(fatal(bool(style.presentation != nullptr)));
    if (!style.presentation) return;
    expect(style.bold);
    expect(style.italic);
    expect(style.underline);
    expect(style.strikethrough);
    expect(style.presentation->foreground == std::optional<Color>{Color(17, 34, 51, 68)});
    expect(style.presentation->background == std::optional<Color>{Color(10, 20, 30)});
    expect(style.presentation->font_family == std::optional<std::string>{"Segoe UI"});
    expect(std::fabs(style.presentation->relative_font_scale - 1.5f) < 0.001f);
    expect(style.presentation->font_weight == std::optional<std::uint16_t>{650});

    const std::unordered_map<std::string, std::string> inner_attributes{{
        "style",
        "font-weight: normal; font-style: normal; text-decoration: none; "
        "font-size: 20px; vertical-align: super"}};
    const auto inner = apply_html_inline_presentation(style, "strong", inner_attributes);
    expect(!inner.bold);
    expect(!inner.italic);
    expect(!inner.underline);
    expect(!inner.strikethrough);
    expect(fatal(bool(inner.presentation != nullptr)));
    if (inner.presentation) {
        expect(inner.presentation->absolute_font_size == std::optional<float>{20.0f});
        expect(inner.presentation->relative_font_scale == 1.0f);
        expect(inner.presentation->font_weight == std::optional<std::uint16_t>{400});
        expect(inner.presentation->baseline == InlineBaseline::Superscript);
        expect(inner.presentation->foreground == style.presentation->foreground);
    }
};

"unsafe_or_unsupported_css_does_not_enter_presentation"_test = [] {
    const std::unordered_map<std::string, std::string> attributes{{
        "style",
        "position:absolute; background:url(javascript:bad); color:var(--x); "
        "font-size:99999px; font-family:'bad;font'; vertical-align:12px"}};
    const auto style = apply_html_inline_presentation(InlineStyle::plain(), "span", attributes);
    expect(fatal(bool(style.presentation != nullptr)));
    if (!style.presentation) return;
    expect(!style.presentation->foreground.has_value());
    expect(!style.presentation->background.has_value());
    expect(!style.presentation->font_family.has_value());
    expect(style.presentation->absolute_font_size == std::optional<float>{144.0f});
    expect(style.presentation->baseline == InlineBaseline::Normal);
};

"small_sub_sup_and_mark_compose_recursively"_test = [] {
    const std::unordered_map<std::string, std::string> empty;
    auto style = apply_html_inline_presentation(InlineStyle::plain(), "small", empty);
    style = apply_html_inline_presentation(style, "sub", empty);
    style = apply_html_inline_presentation(style, "mark", empty);
    expect(fatal(bool(style.presentation != nullptr)));
    if (!style.presentation) return;
    expect(style.presentation->relative_font_scale < 0.70f);
    expect(style.presentation->baseline == InlineBaseline::Subscript);
    expect(style.presentation->highlight);
};

};

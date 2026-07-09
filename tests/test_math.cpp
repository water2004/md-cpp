import std;
#include "test_framework.h"
import elmd.core.types;
import elmd.core.theme;
import elmd.core.utf;
import elmd.core.render_model;
import elmd.core.math_renderer;

using namespace elmd;

ELMD_TEST(test_fallback_inline) {
    FallbackMathRenderer r;
    auto rm = r.render_inline("x^2 + y^2", MathStyle::inline_default());
    ELMD_CHECK(rm.size.width > 0);
    ELMD_CHECK(rm.size.height > 0);
    ELMD_CHECK(rm.kind == RenderedMathKind::PlainTextFallback);
}

ELMD_TEST(test_fallback_block) {
    FallbackMathRenderer r;
    auto rm = r.render_block("E = mc^2", MathStyle::block_default());
    ELMD_CHECK(rm.kind == RenderedMathKind::PlainTextFallback);
}

ELMD_TEST(test_simple_parse) {
    auto pr = parse_math("x^2 + y^2");
    ELMD_CHECK(!pr.tokens.empty());
}

ELMD_TEST(test_command_parse) {
    auto pr = parse_math("\\alpha + \\beta");
    int cmds = 0;
    for (const auto& t : pr.tokens) if (t.kind == MathTokenKind::Command) ++cmds;
    ELMD_CHECK_EQ(cmds, 2);
}

ELMD_TEST(test_math_cache_basic) {
    MathCache c;
    auto k = MathCache::make_key("x^2", 14.0f, 96.0f, 0, MathDisplayMode::Inline);
    ELMD_CHECK(c.get(k) == nullptr);
    c.put(k, RenderedMath{});
    ELMD_CHECK(c.get(k) != nullptr);
}
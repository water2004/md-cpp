#include "elmd_test.hpp"
import elmd.core.types;
import elmd.core.theme;
import elmd.core.utf;
import elmd.core.render_model;
import elmd.core.math_renderer;

using namespace elmd;
using namespace boost::ut;


suite math_tests = [] {

"test_fallback_inline"_test = [] {
    FallbackMathRenderer r;
    auto rm = r.render_inline("x^2 + y^2", MathStyle::inline_default());
    expect(fatal(bool(rm.size.width > 0)));
    expect(fatal(bool(rm.size.height > 0)));
    expect(fatal(bool(rm.kind == RenderedMathKind::PlainTextFallback)));
};

"test_fallback_block"_test = [] {
    FallbackMathRenderer r;
    auto rm = r.render_block("E = mc^2", MathStyle::block_default());
    expect(fatal(bool(rm.kind == RenderedMathKind::PlainTextFallback)));
};

"test_simple_parse"_test = [] {
    auto pr = parse_math("x^2 + y^2");
    expect(fatal(bool(!pr.tokens.empty())));
};

"test_command_parse"_test = [] {
    auto pr = parse_math("\\alpha + \\beta");
    int cmds = 0;
    for (const auto& t : pr.tokens) if (t.kind == MathTokenKind::Command) ++cmds;
    expect(fatal(bool((cmds) == (2))));
};

"test_math_cache_basic"_test = [] {
    MathCache c;
    auto k = MathCache::make_key("x^2", 14.0f, 96.0f, 0, MathDisplayMode::Inline);
    expect(fatal(bool(c.get(k) == nullptr)));
    c.put(k, RenderedMath{});
    expect(fatal(bool(c.get(k) != nullptr)));
};

}; // suite math_tests

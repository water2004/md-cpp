import std;
#include "test_framework.h"
import elmd.core.extension;
import elmd.core.dialect;

using namespace elmd;

ELMD_TEST(test_registry_default_extensions) {
    auto reg = default_extensions();
    auto names = reg.names();
    ELMD_CHECK_EQ(names.size(), 7u);
}

ELMD_TEST(test_registry_can_register_math) {
    ExtensionRegistry reg;
    struct MathExt : MarkdownExtensionBase {
        std::string_view name() const override { return "math"; }
        void configure(MarkdownDialect& d) const override { d.math.inline_dollar = true; }
    };
    reg.register_extension(std::make_shared<MathExt>());
    ELMD_CHECK_EQ(reg.names().size(), 1u);
    ELMD_CHECK_EQ(reg.names()[0], std::string("math"));
}

ELMD_TEST(test_registry_can_register_toc) {
    ExtensionRegistry reg;
    struct TocExt : MarkdownExtensionBase {
        std::string_view name() const override { return "toc"; }
    };
    reg.register_extension(std::make_shared<TocExt>());
    ELMD_CHECK_EQ(reg.names().size(), 1u);
    ELMD_CHECK_EQ(reg.names()[0], std::string("toc"));
}

ELMD_TEST(test_registry_configure_all_applies) {
    ExtensionRegistry reg;
    struct EnableMath : MarkdownExtensionBase {
        std::string_view name() const override { return "math"; }
        void configure(MarkdownDialect& d) const override { d.math.fenced_math = true; }
    };
    reg.register_extension(std::make_shared<EnableMath>());
    MarkdownDialect d;
    d.math.fenced_math = false;
    reg.configure_all(d);
    ELMD_CHECK(d.math.fenced_math == true);
}
import std;
#include "test_framework.h"
import elmd.core.extension;
import elmd.core.dialect;
import elmd.core.editor;
import elmd.core.ast;

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
        void configure(MarkdownDialect&) const override {}
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

ELMD_TEST(test_registry_rejects_duplicate_extension_names) {
    ExtensionRegistry registry;
    struct Named : MarkdownExtensionBase {
        std::string_view name() const override { return "same"; }
        void configure(MarkdownDialect&) const override {}
    };
    ELMD_CHECK(registry.register_extension(std::make_shared<Named>()));
    ELMD_CHECK(!registry.register_extension(std::make_shared<Named>()));
    ELMD_CHECK(registry.find("same") != nullptr);
    ELMD_CHECK_EQ(registry.extensions().size(), 1u);
}

ELMD_TEST(test_extension_configured_dialect_flows_into_editor_ast) {
    MarkdownDialect disabled = default_dialect();
    disabled.math.inline_dollar = false;
    Editor withoutExtension("$x$", disabled);
    ELMD_CHECK(withoutExtension.document().blocks.size() == 1u);
    ELMD_CHECK(withoutExtension.document().blocks.front().children.size() == 1u);
    ELMD_CHECK(withoutExtension.document().blocks.front().children.front().kind == InlineKind::Text);

    ExtensionRegistry registry;
    struct EnableInlineMath : MarkdownExtensionBase {
        std::string_view name() const override { return "inline-math"; }
        void configure(MarkdownDialect& dialect) const override { dialect.math.inline_dollar = true; }
    };
    registry.register_extension(std::make_shared<EnableInlineMath>());
    Editor withExtension("$x$", registry.configured_dialect(disabled));
    ELMD_CHECK(withExtension.document().blocks.size() == 1u);
    ELMD_CHECK(withExtension.document().blocks.front().children.size() == 1u);
    ELMD_CHECK(withExtension.document().blocks.front().children.front().kind == InlineKind::InlineMath);
}

import std;
import boost.ut;
import elmd.core.extension;
import elmd.core.dialect;
import elmd.core.editor;
import elmd.core.ast;

using namespace elmd;
using namespace boost::ut;


suite extension_tests = [] {

"test_registry_default_extensions"_test = [] {
    auto reg = default_extensions();
    auto names = reg.names();
    expect(fatal(bool((names.size()) == (7u))));
};

"test_registry_can_register_math"_test = [] {
    ExtensionRegistry reg;
    struct MathExt : MarkdownExtensionBase {
        std::string_view name() const override { return "math"; }
        void configure(MarkdownDialect& d) const override { d.math.inline_dollar = true; }
    };
    reg.register_extension(std::make_shared<MathExt>());
    expect(fatal(bool((reg.names().size()) == (1u))));
    expect(fatal(bool((reg.names()[0]) == (std::string("math")))));
};

"test_registry_can_register_toc"_test = [] {
    ExtensionRegistry reg;
    struct TocExt : MarkdownExtensionBase {
        std::string_view name() const override { return "toc"; }
        void configure(MarkdownDialect&) const override {}
    };
    reg.register_extension(std::make_shared<TocExt>());
    expect(fatal(bool((reg.names().size()) == (1u))));
    expect(fatal(bool((reg.names()[0]) == (std::string("toc")))));
};

"test_registry_configure_all_applies"_test = [] {
    ExtensionRegistry reg;
    struct EnableMath : MarkdownExtensionBase {
        std::string_view name() const override { return "math"; }
        void configure(MarkdownDialect& d) const override { d.math.fenced_math = true; }
    };
    reg.register_extension(std::make_shared<EnableMath>());
    MarkdownDialect d;
    d.math.fenced_math = false;
    reg.configure_all(d);
    expect(fatal(bool(d.math.fenced_math == true)));
};

"test_registry_rejects_duplicate_extension_names"_test = [] {
    ExtensionRegistry registry;
    struct Named : MarkdownExtensionBase {
        std::string_view name() const override { return "same"; }
        void configure(MarkdownDialect&) const override {}
    };
    expect(fatal(bool(registry.register_extension(std::make_shared<Named>()))));
    expect(fatal(bool(!registry.register_extension(std::make_shared<Named>()))));
    expect(fatal(bool(registry.find("same") != nullptr)));
    expect(fatal(bool((registry.extensions().size()) == (1u))));
};

"test_extension_configured_dialect_flows_into_editor_ast"_test = [] {
    MarkdownDialect disabled = default_dialect();
    disabled.math.inline_dollar = false;
    Editor withoutExtension("$x$", disabled);
    expect(fatal(bool(withoutExtension.document().blocks.size() == 1u)));
    expect(fatal(bool(withoutExtension.document().blocks.front().children.size() == 1u)));
    expect(fatal(bool(withoutExtension.document().blocks.front().children.front().kind == InlineKind::Text)));

    ExtensionRegistry registry;
    struct EnableInlineMath : MarkdownExtensionBase {
        std::string_view name() const override { return "inline-math"; }
        void configure(MarkdownDialect& dialect) const override { dialect.math.inline_dollar = true; }
    };
    registry.register_extension(std::make_shared<EnableInlineMath>());
    Editor withExtension("$x$", registry.configured_dialect(disabled));
    expect(fatal(bool(withExtension.document().blocks.size() == 1u)));
    expect(fatal(bool(withExtension.document().blocks.front().children.size() == 1u)));
    expect(fatal(bool(withExtension.document().blocks.front().children.front().kind == InlineKind::InlineMath)));
};

}; // suite extension_tests

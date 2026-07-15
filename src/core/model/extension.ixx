export module elmd.core.extension;
import std;
import elmd.core.dialect;

export namespace elmd {

class MarkdownExtensionBase {
public:
    virtual ~MarkdownExtensionBase() = default;
    virtual std::string_view name() const = 0;
    virtual std::string_view version() const { return "1.0.0"; }
    virtual void configure(MarkdownDialect& dialect) const = 0;
};

class ExtensionRegistry {
public:
    bool register_extension(std::shared_ptr<MarkdownExtensionBase> extension) {
        if (!extension || extension->name().empty() || find(extension->name())) return false;
        extensions_.push_back(std::move(extension));
        return true;
    }

    MarkdownExtensionBase const* find(std::string_view name) const {
        auto found = std::find_if(extensions_.begin(), extensions_.end(), [&](auto const& extension) {
            return extension->name() == name;
        });
        return found == extensions_.end() ? nullptr : found->get();
    }

    void configure_all(MarkdownDialect& dialect) const {
        for (auto const& extension : extensions_) extension->configure(dialect);
    }

    MarkdownDialect configured_dialect(MarkdownDialect dialect = default_dialect()) const {
        configure_all(dialect);
        return dialect;
    }

    std::span<std::shared_ptr<MarkdownExtensionBase> const> extensions() const { return extensions_; }

    std::vector<std::string> names() const {
        std::vector<std::string> result;
        result.reserve(extensions_.size());
        for (auto const& extension : extensions_) result.emplace_back(extension->name());
        return result;
    }

private:
    std::vector<std::shared_ptr<MarkdownExtensionBase>> extensions_;
};

class DialectExtension final : public MarkdownExtensionBase {
public:
    using Configure = void (*)(MarkdownDialect&);

    DialectExtension(std::string name, Configure configure) : name_(std::move(name)), configure_(configure) {}
    std::string_view name() const override { return name_; }
    void configure(MarkdownDialect& dialect) const override { configure_(dialect); }

private:
    std::string name_;
    Configure configure_;
};

inline ExtensionRegistry default_extensions() {
    ExtensionRegistry registry;
    registry.register_extension(std::make_shared<DialectExtension>("math", [](MarkdownDialect& dialect) {
        dialect.math.inline_dollar = true;
        dialect.math.block_dollar = true;
        dialect.math.inline_paren = true;
        dialect.math.block_bracket = true;
        dialect.math.fenced_math = true;
    }));
    registry.register_extension(std::make_shared<DialectExtension>("toc", [](MarkdownDialect& dialect) {
        dialect.toc.bracket_toc = true;
        dialect.toc.wiki_toc = true;
        dialect.toc.generate_slugs = true;
    }));
    registry.register_extension(std::make_shared<DialectExtension>("frontmatter", [](MarkdownDialect& dialect) {
        dialect.frontmatter.yaml = true;
        dialect.frontmatter.toml = true;
        dialect.frontmatter.json = true;
    }));
    registry.register_extension(std::make_shared<DialectExtension>("table", [](MarkdownDialect& dialect) {
        dialect.gfm.tables = true;
        dialect.tables.gfm_pipe_tables = true;
        dialect.tables.editable_grid_model = true;
    }));
    registry.register_extension(std::make_shared<DialectExtension>("footnote", [](MarkdownDialect& dialect) {
        dialect.footnotes = true;
    }));
    registry.register_extension(std::make_shared<DialectExtension>("callout", [](MarkdownDialect& dialect) {
        dialect.callouts = true;
    }));
    registry.register_extension(std::make_shared<DialectExtension>("wikilink", [](MarkdownDialect& dialect) {
        dialect.wiki_links = true;
    }));
    return registry;
}

}

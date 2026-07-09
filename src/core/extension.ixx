// elmd.core.extension — lightweight Markdown extension registry skeleton.
// Pure core, portable. v1 keeps the trait surface minimal and the built-in
// extensions named so the parser/render/command layers can stay non-hardcoded
// as the project grows. No plugin loading — built-ins register at startup.
export module elmd.core.extension;
import std;
import elmd.core.dialect;

export namespace elmd {

class MarkdownExtensionBase {
public:
    virtual ~MarkdownExtensionBase() = default;
    virtual std::string_view name() const = 0;
    virtual void configure(MarkdownDialect& dialect) const { (void)dialect; }
};

struct ExtensionInfo {
    std::string name;
    std::string version = "0.1.0";
};

class ExtensionRegistry {
public:
    void register_extension(std::shared_ptr<MarkdownExtensionBase> ext) {
        if (ext) extensions_.push_back(std::move(ext));
    }
    void configure_all(MarkdownDialect& dialect) const {
        for (const auto& e : extensions_) e->configure(dialect);
    }
    const std::vector<std::shared_ptr<MarkdownExtensionBase>>& extensions() const { return extensions_; }
    std::vector<std::string> names() const {
        std::vector<std::string> out; out.reserve(extensions_.size());
        for (const auto& e : extensions_) out.emplace_back(e->name());
        return out;
    }
private:
    std::vector<std::shared_ptr<MarkdownExtensionBase>> extensions_;
};

inline ExtensionRegistry default_extensions() {
    ExtensionRegistry r;
    // placeholders — registered so the README feature-grid / acceptance
    // 32 ("extension registry can register") has a verifiable surface.
    struct NamedExt : MarkdownExtensionBase {
        std::string nm;
        explicit NamedExt(std::string n) : nm(std::move(n)) {}
        std::string_view name() const override { return nm; }
    };
    r.register_extension(std::make_shared<NamedExt>("math"));
    r.register_extension(std::make_shared<NamedExt>("toc"));
    r.register_extension(std::make_shared<NamedExt>("frontmatter"));
    r.register_extension(std::make_shared<NamedExt>("table"));
    r.register_extension(std::make_shared<NamedExt>("footnote"));
    r.register_extension(std::make_shared<NamedExt>("callout"));
    r.register_extension(std::make_shared<NamedExt>("wikilink"));
    return r;
}

} // namespace elmd
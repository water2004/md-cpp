// folia.core.metadata — document metadata (parsed-from-frontmatter).
export module folia.core.metadata;
import std;
import folia.core.dialect;
import folia.core.utf;

export namespace folia {

struct DocumentMetadata {
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> date;
    std::vector<std::string> tags;
    std::string custom;                  // raw text format (no JSON dep in v1)
    std::optional<FrontmatterFormat> frontmatter_format;
    std::optional<std::string> frontmatter_raw;
    std::size_t word_count = 0;
    std::size_t char_count = 0;
    std::size_t line_count = 0;
};

// Lightweight frontmatter to metadata: extract a few keys (title/author/date/tags)
// from YAML-ish or plain text. A full YAML parser is out of scope at v1.
inline DocumentMetadata from_frontmatter(const std::string& raw, FrontmatterFormat fmt) {
    DocumentMetadata m;
    m.frontmatter_format = fmt;
    m.frontmatter_raw = raw;
    auto trim = [](std::string s) {
        auto a = s.find_first_not_of(" \t");
        auto b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string{};
        return s.substr(a, (b == std::string::npos ? s.size() : b - a + 1));
    };
    auto lines_of = [](const std::string& s) {
        std::vector<std::string> out;
        std::string acc;
        for (char c : s + "\n") {
            if (c == '\n') { out.push_back(acc); acc.clear(); }
            else acc.push_back(c);
        }
        return out;
    };
    auto set_str_kv = [&](const std::string& key, std::function<void(std::string)> sink) {
        // match "key: value" on a line by itself (YAML/TOML basics both use `key = value` or `key: value`)
        for (const auto& raw_line : lines_of(raw)) {
            std::string t = trim(raw_line);
            if (t.empty()) continue;
            // strip optional leading `+` for TOML
            if (!t.empty() && t.front() == '+') t.erase(0, 1);
            // `key: value` or `key = value`
            auto find_kv = [&](char sep) -> std::pair<std::string, std::string> {
                auto p = t.find(sep);
                if (p == std::string::npos) return {};
                return {trim(t.substr(0, p)), trim(t.substr(p + 1))};
            };
            for (char sep : {':', '='}) {
                auto [k, v] = find_kv(sep);
                if (k == key && !v.empty()) {
                    // strip surrounding quotes
                    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
                        v = v.substr(1, v.size() - 2);
                    if (k == "tags" && !v.empty() && (v.front() == '[')) {
                        // inline list [a, b]
                        std::string body = v.substr(1, v.empty() ? 0 : v.size() - 2);
                        std::string acc;
                        for (char c : body + ",") {
                            if (c == ',') { auto t2 = trim(acc); if (!t2.empty()) m.tags.push_back(t2); acc.clear(); }
                            else acc.push_back(c);
                        }
                    } else {
                        sink(v);
                    }
                }
            }
        }
    };
    set_str_kv("title", [&](std::string v){ m.title = v; });
    set_str_kv("author", [&](std::string v){ m.author = v; });
    set_str_kv("date", [&](std::string v){ m.date = v; });
    // tags: try set_str_kv on "tags" too for YAML/TOML array literal
    m.custom = raw;
    return m;
}

} // namespace folia
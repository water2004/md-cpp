#include "pch.h"
#include "media/TreeSitterHighlighter.h"

#include "tree_sitter/api.h"

extern "C"
{
    const TSLanguage* tree_sitter_cpp();
    const TSLanguage* tree_sitter_css();
    const TSLanguage* tree_sitter_go();
    const TSLanguage* tree_sitter_html();
    const TSLanguage* tree_sitter_javascript();
    const TSLanguage* tree_sitter_java();
    const TSLanguage* tree_sitter_json();
    const TSLanguage* tree_sitter_python();
    const TSLanguage* tree_sitter_rust();
    const TSLanguage* tree_sitter_typescript();
}

namespace
{
    std::string NormalizeLanguage(std::string_view language)
    {
        std::string value(language);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), value.end());
        if (value == "c" || value == "h" || value == "c++" || value == "cc" || value == "cxx" || value == "hpp") return "cpp";
        if (value == "js" || value == "jsx" || value == "node") return "javascript";
        if (value == "ts") return "typescript";
        if (value == "py") return "python";
        if (value == "rs") return "rust";
        if (value == "golang") return "go";
        if (value == "htm") return "html";
        return value;
    }

    const TSLanguage* LanguageFor(std::string const& language)
    {
        if (language == "cpp") return tree_sitter_cpp();
        if (language == "css") return tree_sitter_css();
        if (language == "go") return tree_sitter_go();
        if (language == "html") return tree_sitter_html();
        if (language == "javascript") return tree_sitter_javascript();
        if (language == "java") return tree_sitter_java();
        if (language == "json" || language == "jsonc") return tree_sitter_json();
        if (language == "python") return tree_sitter_python();
        if (language == "rust") return tree_sitter_rust();
        if (language == "typescript") return tree_sitter_typescript();
        return nullptr;
    }

    bool Contains(std::string_view value, std::string_view part)
    {
        return value.find(part) != std::string_view::npos;
    }

    bool IsKeyword(std::string_view value)
    {
        static const std::unordered_set<std::string_view> values{
            "as", "async", "await", "break", "case", "catch", "class", "const", "continue", "co_await", "co_return", "co_yield",
            "default", "defer", "delete", "do", "else", "enum", "export", "extends", "extern", "false", "finally", "fn", "for", "foreach",
            "from", "func", "function", "if", "implements", "import", "in", "instanceof", "interface", "let", "match", "namespace", "new", "nil",
            "none", "nullptr", "package", "pass", "private", "protected", "public", "raise", "return", "self", "sizeof", "static", "struct",
            "super", "switch", "template", "this", "throw", "trait", "true", "try", "type", "typeof", "typename", "union", "unsafe", "use",
            "using", "var", "virtual", "void", "volatile", "where", "while", "with", "yield"
        };
        return values.contains(value);
    }

    bool IsPrimitive(std::string_view value)
    {
        static const std::unordered_set<std::string_view> values{
            "any", "bool", "boolean", "byte", "char", "decimal", "double", "f32", "f64", "float", "i8", "i16", "i32", "i64", "int", "long",
            "never", "number", "object", "short", "signed", "str", "string", "u8", "u16", "u32", "u64", "uint", "ulong", "undefined", "unsigned"
        };
        return values.contains(value);
    }

    bool IsOperator(std::string_view value)
    {
        if (value.empty() || value.size() > 4) return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::string_view("+-*/%=!<>&|^~?:.@#").find(static_cast<char>(ch)) != std::string_view::npos;
        });
    }

    winrt::ElMd::SyntaxHighlightKind ClassifyLeaf(TSNode node, winrt::ElMd::SyntaxHighlightKind inherited)
    {
        using Kind = winrt::ElMd::SyntaxHighlightKind;
        if (inherited != Kind::None) return inherited;
        std::string_view type = ts_node_type(node);
        if (Contains(type, "comment")) return Kind::Comment;
        if (Contains(type, "string") || Contains(type, "char_literal") || Contains(type, "template_literal")) return Kind::String;
        if (Contains(type, "number") || Contains(type, "integer") || Contains(type, "float")) return Kind::Number;
        if (Contains(type, "type_identifier") || Contains(type, "primitive_type") || IsPrimitive(type)) return Kind::Type;
        if (Contains(type, "field_identifier") || Contains(type, "property_identifier") || Contains(type, "attribute_name")) return Kind::Property;
        if (Contains(type, "constant") || Contains(type, "enum_member")) return Kind::Constant;
        if (IsKeyword(type)) return Kind::Keyword;
        if (IsOperator(type)) return Kind::Operator;
        if (type == "identifier") {
            auto parent = ts_node_parent(node);
            for (int depth = 0; depth < 4 && !ts_node_is_null(parent); ++depth) {
                std::string_view parentType = ts_node_type(parent);
                if (Contains(parentType, "call") || Contains(parentType, "function") || Contains(parentType, "method") || Contains(parentType, "constructor")) return Kind::Function;
                if (Contains(parentType, "type")) return Kind::Type;
                parent = ts_node_parent(parent);
            }
        }
        return Kind::None;
    }

    winrt::ElMd::SyntaxHighlightKind InheritedKind(TSNode node, winrt::ElMd::SyntaxHighlightKind inherited)
    {
        using Kind = winrt::ElMd::SyntaxHighlightKind;
        if (inherited != Kind::None) return inherited;
        std::string_view type = ts_node_type(node);
        if (Contains(type, "comment")) return Kind::Comment;
        if (Contains(type, "string") || Contains(type, "char_literal") || Contains(type, "template_literal")) return Kind::String;
        if (type.starts_with("preproc") || type.starts_with("preprocessor")) return Kind::Preprocessor;
        return Kind::None;
    }

    void Collect(TSNode node, winrt::ElMd::SyntaxHighlightKind inherited, std::vector<winrt::ElMd::SyntaxHighlightRange>& ranges, std::vector<std::uint32_t> const& byteToCodepoint)
    {
        inherited = InheritedKind(node, inherited);
        auto childCount = ts_node_child_count(node);
        if (childCount == 0) {
            auto kind = ClassifyLeaf(node, inherited);
            auto startByte = (std::min)(static_cast<std::size_t>(ts_node_start_byte(node)), byteToCodepoint.size() - 1);
            auto endByte = (std::min)(static_cast<std::size_t>(ts_node_end_byte(node)), byteToCodepoint.size() - 1);
            auto start = byteToCodepoint[startByte];
            auto end = byteToCodepoint[endByte];
            if (kind != winrt::ElMd::SyntaxHighlightKind::None && start < end) ranges.push_back({start, end - start, kind});
            return;
        }
        for (std::uint32_t index = 0; index < childCount; ++index) Collect(ts_node_child(node, index), inherited, ranges, byteToCodepoint);
    }

    std::vector<std::uint32_t> ByteToCodepointMap(std::string_view source)
    {
        std::vector<std::uint32_t> result(source.size() + 1);
        std::size_t byte = 0;
        std::uint32_t codepoint = 0;
        while (byte < source.size()) {
            auto first = static_cast<unsigned char>(source[byte]);
            std::size_t length = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
            length = (std::min)(length, source.size() - byte);
            for (std::size_t index = 0; index < length; ++index) result[byte + index] = codepoint;
            byte += length;
            result[byte] = ++codepoint;
        }
        return result;
    }
}

namespace winrt::ElMd
{
    struct TreeSitterHighlighter::State
    {
        struct ParserDeleter
        {
            void operator()(TSParser* parser) const { ts_parser_delete(parser); }
        };

        std::unordered_map<std::string, std::unique_ptr<TSParser, ParserDeleter>> parsers;
        std::unordered_map<std::string, std::vector<SyntaxHighlightRange>> cache;
        std::deque<std::string> cacheOrder;
        std::size_t cacheBytes = 0;

        TSParser* ParserFor(std::string const& language)
        {
            auto found = parsers.find(language);
            if (found != parsers.end()) return found->second.get();
            auto grammar = LanguageFor(language);
            if (!grammar) return nullptr;
            auto parser = std::unique_ptr<TSParser, ParserDeleter>(ts_parser_new());
            if (!parser || !ts_parser_set_language(parser.get(), grammar)) return nullptr;
            auto value = parser.get();
            parsers.emplace(language, std::move(parser));
            return value;
        }

        void Store(std::string key, std::vector<SyntaxHighlightRange> ranges)
        {
            auto bytes = key.size() + ranges.size() * sizeof(SyntaxHighlightRange);
            while ((!cacheOrder.empty()) && (cache.size() >= 128 || cacheBytes + bytes > 4 * 1024 * 1024)) {
                auto oldest = std::move(cacheOrder.front());
                cacheOrder.pop_front();
                auto found = cache.find(oldest);
                if (found == cache.end()) continue;
                cacheBytes -= found->first.size() + found->second.size() * sizeof(SyntaxHighlightRange);
                cache.erase(found);
            }
            cacheBytes += bytes;
            cacheOrder.push_back(key);
            cache.emplace(std::move(key), std::move(ranges));
        }
    };

    TreeSitterHighlighter::TreeSitterHighlighter() : state(std::make_unique<State>()) {}
    TreeSitterHighlighter::~TreeSitterHighlighter() = default;

    std::vector<SyntaxHighlightRange> TreeSitterHighlighter::Highlight(std::string_view language, std::string_view source)
    {
        auto normalized = NormalizeLanguage(language);
        std::string key = normalized;
        key.push_back('\0');
        key.append(source);
        if (auto found = state->cache.find(key); found != state->cache.end()) return found->second;
        std::vector<SyntaxHighlightRange> ranges;
        auto parser = state->ParserFor(normalized);
        if (!parser || source.empty()) return ranges;
        auto tree = ts_parser_parse_string(parser, nullptr, source.data(), static_cast<std::uint32_t>(source.size()));
        if (!tree) return ranges;
        auto map = ByteToCodepointMap(source);
        Collect(ts_tree_root_node(tree), SyntaxHighlightKind::None, ranges, map);
        ts_tree_delete(tree);
        std::sort(ranges.begin(), ranges.end(), [](auto const& left, auto const& right) { return left.start < right.start; });
        std::vector<SyntaxHighlightRange> merged;
        for (auto const& range : ranges) {
            if (!merged.empty() && merged.back().kind == range.kind && merged.back().start + merged.back().length == range.start) merged.back().length += range.length;
            else merged.push_back(range);
        }
        state->Store(std::move(key), merged);
        return merged;
    }

    void TreeSitterHighlighter::Clear()
    {
        state->cache.clear();
        state->cacheOrder.clear();
        state->cacheBytes = 0;
    }
}

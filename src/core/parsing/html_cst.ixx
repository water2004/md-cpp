module;

#include <tree_sitter/api.h>

export module elmd.core.html_cst;
import std;
import elmd.core.text_edit;
import elmd.core.utf;

extern "C" const TSLanguage* tree_sitter_html();

export namespace elmd {

enum class HtmlTokenKind {
    Text,
    Tag,
    Attribute,
    Comment,
    Doctype,
    Raw,
    Error,
};

struct HtmlToken {
    SourceRange range;
    HtmlTokenKind kind = HtmlTokenKind::Raw;
};

enum class HtmlCstKind {
    Element,
    Text,
    Comment,
    Doctype,
    Raw,
    Error,
};

enum class HtmlParseStatus {
    Complete,
    MissingCloser,
    Invalid,
};

struct HtmlAttribute {
    std::string name;
    SourceRange range;
    SourceRange name_range;
    std::optional<SourceRange> value_range;
};

struct HtmlCstNode {
    HtmlCstKind kind = HtmlCstKind::Raw;
    SourceRange range;
    HtmlParseStatus status = HtmlParseStatus::Complete;
    std::string tag_name;
    SourceRange opening;
    SourceRange content;
    std::optional<SourceRange> closing;
    std::vector<HtmlAttribute> attributes;
    std::vector<HtmlCstNode> children;
    bool self_closing = false;
};

struct HtmlCstTree {
    std::vector<HtmlCstNode> nodes;
    std::vector<HtmlToken> tokens;
    bool has_error = false;
};

inline std::u32string flatten_html_tokens(
    const HtmlCstTree& tree,
    std::u32string_view source) {
    std::u32string result;
    result.reserve(source.size());
    std::size_t cursor = 0;
    for (const auto& token : tree.tokens) {
        if (!token.range.valid_for(source.size())
            || token.range.empty()
            || token.range.start != cursor) {
            return {};
        }
        result.append(source.substr(token.range.start, token.range.length()));
        cursor = token.range.end;
    }
    return cursor == source.size() ? result : std::u32string{};
}

inline bool html_tokens_partition_source(
    const HtmlCstTree& tree,
    std::size_t source_length) {
    std::size_t cursor = 0;
    for (const auto& token : tree.tokens) {
        if (!token.range.valid_for(source_length)
            || token.range.empty()
            || token.range.start != cursor) {
            return false;
        }
        cursor = token.range.end;
    }
    return cursor == source_length;
}

inline bool html_is_void_element(std::string_view name) {
    static const std::unordered_set<std::string_view> names{
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr",
    };
    return names.contains(name);
}

inline bool html_is_unsafe_element(std::string_view name) {
    static const std::unordered_set<std::string_view> names{
        "script", "style", "iframe", "object", "embed", "form", "input",
        "button", "textarea", "select", "option", "canvas",
    };
    return names.contains(name);
}

inline bool html_is_block_element(std::string_view name) {
    static const std::unordered_set<std::string_view> names{
        "address", "article", "aside", "blockquote", "body", "caption",
        "center", "col", "colgroup", "dd", "details", "dialog", "dir",
        "div", "dl", "dt", "fieldset", "figcaption", "figure", "footer",
        "form", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header",
        "hr", "html", "legend", "li", "main", "menu", "nav", "ol", "p",
        "pre", "section", "summary", "table", "tbody", "td", "tfoot", "th",
        "thead", "title", "tr", "ul",
        // Unsafe elements still need block recognition so they become one
        // inert, lossless UnsupportedMarkup node rather than slipping through
        // as inline text at a line start.
        "script", "style", "iframe", "object", "embed", "input", "button",
        "textarea", "select", "option", "canvas",
    };
    return names.contains(name);
}

namespace html_cst_detail {

struct ParserDeleter {
    void operator()(TSParser* value) const { ts_parser_delete(value); }
};

struct TreeDeleter {
    void operator()(TSTree* value) const { ts_tree_delete(value); }
};

struct EncodedSource {
    std::string utf8;
    std::vector<std::size_t> byte_to_source;
};

inline std::size_t utf8_width(char32_t cp) {
    if (cp <= 0x7f) return 1;
    if (cp <= 0x7ff) return 2;
    if (cp <= 0xffff) return 3;
    return 4;
}

inline EncodedSource encode(std::u32string_view source) {
    EncodedSource encoded;
    encoded.utf8 = cps_to_utf8(source);
    encoded.byte_to_source.resize(encoded.utf8.size() + 1, source.size());
    std::size_t byte = 0;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto width = utf8_width(source[index]);
        for (std::size_t offset = 0;
             offset < width && byte + offset < encoded.byte_to_source.size();
             ++offset) {
            encoded.byte_to_source[byte + offset] = index;
        }
        byte += width;
        if (byte < encoded.byte_to_source.size()) {
            encoded.byte_to_source[byte] = index + 1;
        }
    }
    return encoded;
}

inline SourceRange source_range(TSNode node, const EncodedSource& source) {
    const auto start_byte = (std::min)(
        static_cast<std::size_t>(ts_node_start_byte(node)),
        source.utf8.size());
    const auto end_byte = (std::min)(
        static_cast<std::size_t>(ts_node_end_byte(node)),
        source.utf8.size());
    return {
        source.byte_to_source[start_byte],
        source.byte_to_source[end_byte],
    };
}

inline std::string lower_ascii(std::u32string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto cp : value) {
        if (cp >= U'A' && cp <= U'Z') result.push_back(static_cast<char>(cp - U'A' + U'a'));
        else if (cp <= 0x7f) result.push_back(static_cast<char>(cp));
    }
    return result;
}

inline HtmlTokenKind token_kind(std::string_view type, bool error) {
    if (error || type == "ERROR" || type == "erroneous_end_tag_name") {
        return HtmlTokenKind::Error;
    }
    if (type == "comment") return HtmlTokenKind::Comment;
    if (type == "doctype" || type == "doctype_token1") return HtmlTokenKind::Doctype;
    if (type == "attribute_name" || type == "attribute_value") return HtmlTokenKind::Attribute;
    if (type == "text" || type == "raw_text") return HtmlTokenKind::Text;
    if (type == "<" || type == ">" || type == "</" || type == "/>"
        || type == "=" || type == "tag_name") {
        return HtmlTokenKind::Tag;
    }
    return HtmlTokenKind::Raw;
}

inline void collect_leaf_tokens(
    TSNode node,
    const EncodedSource& source,
    std::vector<HtmlToken>& leaves) {
    const auto count = ts_node_child_count(node);
    if (count == 0) {
        const auto range = source_range(node, source);
        if (!range.empty()) {
            leaves.push_back({
                range,
                token_kind(ts_node_type(node), ts_node_is_error(node)),
            });
        }
        return;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        collect_leaf_tokens(ts_node_child(node, index), source, leaves);
    }
}

inline std::vector<HtmlToken> partition_tokens(
    TSNode root,
    const EncodedSource& encoded,
    std::size_t source_length) {
    std::vector<HtmlToken> leaves;
    collect_leaf_tokens(root, encoded, leaves);
    std::ranges::sort(leaves, [](const auto& left, const auto& right) {
        if (left.range.start != right.range.start) return left.range.start < right.range.start;
        return left.range.end < right.range.end;
    });

    std::vector<HtmlToken> tokens;
    std::size_t cursor = 0;
    for (auto leaf : leaves) {
        if (leaf.range.end <= cursor) continue;
        leaf.range.start = (std::max)(leaf.range.start, cursor);
        if (leaf.range.start > cursor) {
            tokens.push_back({{cursor, leaf.range.start}, HtmlTokenKind::Raw});
        }
        tokens.push_back(leaf);
        cursor = leaf.range.end;
    }
    if (cursor < source_length) {
        tokens.push_back({{cursor, source_length}, HtmlTokenKind::Raw});
    }
    return tokens;
}

inline std::optional<TSNode> direct_child(TSNode node, std::string_view type) {
    const auto count = ts_node_named_child_count(node);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto child = ts_node_named_child(node, index);
        if (std::string_view{ts_node_type(child)} == type) return child;
    }
    return std::nullopt;
}

inline std::optional<TSNode> descendant(TSNode node, std::string_view type) {
    if (std::string_view{ts_node_type(node)} == type) return node;
    const auto count = ts_node_named_child_count(node);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (auto result = descendant(ts_node_named_child(node, index), type)) return result;
    }
    return std::nullopt;
}

inline std::vector<HtmlAttribute> attributes(
    TSNode opening,
    std::u32string_view source,
    const EncodedSource& encoded) {
    std::vector<HtmlAttribute> result;
    const auto count = ts_node_named_child_count(opening);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto child = ts_node_named_child(opening, index);
        if (std::string_view{ts_node_type(child)} != "attribute") continue;
        const auto name = descendant(child, "attribute_name");
        if (!name) continue;
        HtmlAttribute attribute;
        attribute.range = source_range(child, encoded);
        attribute.name_range = source_range(*name, encoded);
        attribute.name = lower_ascii(source.substr(
            attribute.name_range.start,
            attribute.name_range.length()));
        if (auto value = descendant(child, "attribute_value")) {
            attribute.value_range = source_range(*value, encoded);
        }
        result.push_back(std::move(attribute));
    }
    return result;
}

inline std::optional<HtmlCstNode> build_node(
    TSNode node,
    std::u32string_view source,
    const EncodedSource& encoded);

inline void append_content_children(
    TSNode element,
    std::u32string_view source,
    const EncodedSource& encoded,
    std::vector<HtmlCstNode>& target) {
    const auto count = ts_node_named_child_count(element);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto child = ts_node_named_child(element, index);
        const std::string_view type{ts_node_type(child)};
        if (type == "start_tag" || type == "self_closing_tag" || type == "end_tag") continue;
        if (auto converted = build_node(child, source, encoded)) {
            target.push_back(std::move(*converted));
        }
    }
}

inline std::optional<HtmlCstNode> build_element(
    TSNode node,
    std::u32string_view source,
    const EncodedSource& encoded) {
    auto opening = direct_child(node, "start_tag");
    auto self_closing = direct_child(node, "self_closing_tag");
    if (!opening && self_closing) opening = self_closing;
    if (!opening) return std::nullopt;

    HtmlCstNode result;
    result.kind = HtmlCstKind::Element;
    result.range = source_range(node, encoded);
    result.opening = source_range(*opening, encoded);
    result.self_closing = self_closing.has_value();
    if (auto tag = descendant(*opening, "tag_name")) {
        const auto range = source_range(*tag, encoded);
        result.tag_name = lower_ascii(source.substr(range.start, range.length()));
    }
    result.attributes = attributes(*opening, source, encoded);
    if (auto closing = direct_child(node, "end_tag")) {
        result.closing = source_range(*closing, encoded);
    }
    result.content = {
        result.opening.end,
        result.closing ? result.closing->start : result.range.end,
    };
    append_content_children(node, source, encoded, result.children);

    const auto missing_closer = !result.self_closing
        && !html_is_void_element(result.tag_name)
        && !result.closing;
    if (ts_node_has_error(node)) result.status = HtmlParseStatus::Invalid;
    else if (missing_closer) result.status = HtmlParseStatus::MissingCloser;
    return result;
}

inline std::optional<HtmlCstNode> build_node(
    TSNode node,
    std::u32string_view source,
    const EncodedSource& encoded) {
    const std::string_view type{ts_node_type(node)};
    if (type == "element" || type == "script_element" || type == "style_element") {
        return build_element(node, source, encoded);
    }

    HtmlCstNode result;
    result.range = source_range(node, encoded);
    if (result.range.empty() && !ts_node_is_missing(node)) return std::nullopt;
    if (type == "text" || type == "raw_text") result.kind = HtmlCstKind::Text;
    else if (type == "comment") result.kind = HtmlCstKind::Comment;
    else if (type == "doctype") result.kind = HtmlCstKind::Doctype;
    else if (type == "ERROR" || type == "erroneous_end_tag") {
        result.kind = HtmlCstKind::Error;
        result.status = HtmlParseStatus::Invalid;
    } else {
        result.kind = HtmlCstKind::Raw;
        if (ts_node_has_error(node)) result.status = HtmlParseStatus::Invalid;
    }
    return result;
}

} // namespace html_cst_detail

inline HtmlCstTree parse_html_cst(std::u32string_view source) {
    HtmlCstTree result;
    if (source.empty()) return result;

    const auto encoded = html_cst_detail::encode(source);
    auto parser = std::unique_ptr<TSParser, html_cst_detail::ParserDeleter>{ts_parser_new()};
    if (!parser || !ts_parser_set_language(parser.get(), tree_sitter_html())) {
        result.tokens.push_back({{0, source.size()}, HtmlTokenKind::Error});
        result.nodes.push_back({HtmlCstKind::Error, {0, source.size()}, HtmlParseStatus::Invalid});
        result.has_error = true;
        return result;
    }
    auto tree = std::unique_ptr<TSTree, html_cst_detail::TreeDeleter>{
        ts_parser_parse_string(
            parser.get(),
            nullptr,
            encoded.utf8.data(),
            static_cast<std::uint32_t>(encoded.utf8.size()))};
    if (!tree) {
        result.tokens.push_back({{0, source.size()}, HtmlTokenKind::Error});
        result.nodes.push_back({HtmlCstKind::Error, {0, source.size()}, HtmlParseStatus::Invalid});
        result.has_error = true;
        return result;
    }

    const auto root = ts_tree_root_node(tree.get());
    result.has_error = ts_node_has_error(root);
    result.tokens = html_cst_detail::partition_tokens(root, encoded, source.size());
    const auto count = ts_node_named_child_count(root);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (auto node = html_cst_detail::build_node(
                ts_node_named_child(root, index), source, encoded)) {
            result.nodes.push_back(std::move(*node));
        }
    }
    if (result.nodes.empty()) {
        result.nodes.push_back({
            result.has_error ? HtmlCstKind::Error : HtmlCstKind::Raw,
            {0, source.size()},
            result.has_error ? HtmlParseStatus::Invalid : HtmlParseStatus::Complete,
        });
    }
    return result;
}

} // namespace elmd

// elmd.core.inline_cst — lossless editable inline concrete syntax tree.
export module elmd.core.inline_cst;
import std;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.text_edit;

export namespace elmd {

enum class ParseStatus {
    Complete,
    MissingCloser,
    Invalid,
    Ambiguous,
};

// The token stream is a non-overlapping, exhaustive partition of source.
enum class TokenKind {
    Text,
    Whitespace,
    SoftBreak,
    HardBreak,
    Escape,
    Entity,
    Delimiter,
    Raw,
};

struct InlineToken {
    NodeId id{};
    TokenKind kind = TokenKind::Text;
    SourceRange range;
};

enum class InlineCstKind {
    Text, Whitespace, SoftBreak, HardBreak, Escape, Entity, Delimiter, Raw,
    CodeSpan, InlineMath, Emphasis, Strong, Strikethrough,
    Link, Image, Autolink, HtmlElement,
    FootnoteRef, WikiLink, Extension,
    Error,
    Incomplete,
};

struct DelimitedRanges {
    SourceRange full;
    SourceRange opening;
    SourceRange content;
    std::optional<SourceRange> closing;
};

struct InlineCstNode {
    NodeId id{};
    InlineCstKind kind = InlineCstKind::Text;
    SourceRange range;
    ParseStatus status = ParseStatus::Complete;
    DelimitedRanges delim;
    std::vector<InlineCstNode> children;

    // Derived semantic data for rendering/querying. It is never authoritative
    // for editing or serialization.
    std::string href;
    std::optional<std::string> title;
    std::string alt;
    std::string label;
    std::string target;
    std::optional<std::string> alias;
    MathDelimiter math_delim = MathDelimiter::InlineDollar;
    std::string ext_name;
    std::optional<float> image_width;
    std::optional<float> image_height;
};

using InlineCstNodes = std::vector<InlineCstNode>;

struct InlineCstTree {
    InlineCstNodes nodes;
    std::vector<InlineToken> tokens;
};

inline std::u32string flatten_tokens(const InlineCstTree& tree, std::u32string_view source) {
    std::u32string flattened;
    flattened.reserve(source.size());
    for (const auto& token : tree.tokens) {
        if (!token.range.valid_for(source.size())) return {};
        flattened.append(source.substr(token.range.start, token.range.length()));
    }
    return flattened;
}

inline std::u32string serialize_lossless(const InlineCstTree& tree, std::u32string_view source) {
    return flatten_tokens(tree, source);
}

inline bool tokens_partition_source(const InlineCstTree& tree, std::size_t source_length) {
    std::size_t cursor = 0;
    for (const auto& token : tree.tokens) {
        if (!token.range.valid_for(source_length) || token.range.empty() || token.range.start != cursor) {
            return false;
        }
        cursor = token.range.end;
    }
    return cursor == source_length;
}

inline bool roots_partition_source(const InlineCstTree& tree, std::size_t source_length) {
    std::size_t cursor = 0;
    for (const auto& node : tree.nodes) {
        if (!node.range.valid_for(source_length) || node.range.empty() || node.range.start != cursor) {
            return false;
        }
        cursor = node.range.end;
    }
    return cursor == source_length;
}

} // namespace elmd

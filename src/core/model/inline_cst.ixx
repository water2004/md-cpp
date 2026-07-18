// folia.core.inline_cst — lossless editable inline concrete syntax tree.
export module folia.core.inline_cst;
import std;
import folia.core.ids;
import folia.core.dialect;
import folia.core.image_dimension;
import folia.core.text_edit;

export namespace folia {

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
    // Tokens are an ordered exhaustive partition, so each start is the
    // previous token's end.  Keep only end+kind in one word instead of
    // storing a second stable identity and two machine-word offsets for every
    // lexical run. Interactive identity belongs to structural CST nodes.
    std::uint64_t packed_end_and_kind = 0;

    static constexpr std::uint64_t kind_bits = 3;
    static constexpr std::uint64_t kind_mask = (std::uint64_t{1} << kind_bits) - 1;
    static constexpr std::uint64_t maximum_end =
        (std::numeric_limits<std::uint64_t>::max)() >> kind_bits;

    InlineToken() = default;
    InlineToken(TokenKind token_kind, std::size_t end) {
        if (end > maximum_end) throw std::length_error("inline token offset exceeds compact range");
        packed_end_and_kind = (static_cast<std::uint64_t>(end) << kind_bits)
            | static_cast<std::uint64_t>(token_kind);
    }

    TokenKind kind() const {
        return static_cast<TokenKind>(packed_end_and_kind & kind_mask);
    }

    std::size_t end() const {
        return static_cast<std::size_t>(packed_end_and_kind >> kind_bits);
    }
};

static_assert(sizeof(InlineToken) == sizeof(std::uint64_t));

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

struct InlineCstSemantic {
    std::string href;
    std::optional<std::string> title;
    std::string alt;
    std::string label;
    std::string target;
    std::optional<std::string> alias;
    MathDelimiter math_delim = MathDelimiter::InlineDollar;
    std::string ext_name;
    std::optional<ImageDimension> image_width;
    std::optional<ImageDimension> image_height;
    std::string html_tag;
    // HTML is a lossless embedded syntax island.  The exact attribute
    // spelling remains in InlineDocument.source; this case-folded projection
    // is used only by safe rendering and interaction policy.
    std::unordered_map<std::string, std::string> html_attributes;
};

struct InlineCstNode {
    NodeId id{};
    InlineCstKind kind = InlineCstKind::Text;
    SourceRange range;
    ParseStatus status = ParseStatus::Complete;
    std::vector<InlineCstNode> children;
    // Plain text is by far the most frequent CST node. Delimiter ranges and
    // semantic projection data are therefore allocated only for node kinds
    // that use them instead of widening every text node.
    std::shared_ptr<DelimitedRanges> delimited;
    std::shared_ptr<InlineCstSemantic> semantic;

    DelimitedRanges const& delimiter_ranges() const {
        static const DelimitedRanges empty{};
        return delimited ? *delimited : empty;
    }

    DelimitedRanges& ensure_delimiter_ranges() {
        if (!delimited) delimited = std::make_shared<DelimitedRanges>();
        return *delimited;
    }

    InlineCstSemantic const& semantics() const {
        static const InlineCstSemantic empty{};
        return semantic ? *semantic : empty;
    }

    InlineCstSemantic& ensure_semantics() {
        if (!semantic) semantic = std::make_shared<InlineCstSemantic>();
        return *semantic;
    }
};

using InlineCstNodes = std::vector<InlineCstNode>;

struct InlineCstTree {
    InlineCstNodes nodes;
    std::vector<InlineToken> tokens;
};

inline std::u32string flatten_tokens(const InlineCstTree& tree, std::u32string_view source) {
    std::u32string flattened;
    flattened.reserve(source.size());
    std::size_t cursor = 0;
    for (const auto& token : tree.tokens) {
        const auto end = token.end();
        if (end <= cursor || end > source.size()) return {};
        flattened.append(source.substr(cursor, end - cursor));
        cursor = end;
    }
    return cursor == source.size() ? flattened : std::u32string{};
}

inline std::u32string serialize_lossless(const InlineCstTree& tree, std::u32string_view source) {
    return flatten_tokens(tree, source);
}

inline bool tokens_partition_source(const InlineCstTree& tree, std::size_t source_length) {
    std::size_t cursor = 0;
    for (const auto& token : tree.tokens) {
        const auto end = token.end();
        if (end <= cursor || end > source_length) return false;
        cursor = end;
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

} // namespace folia

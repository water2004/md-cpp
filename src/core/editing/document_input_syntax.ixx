// Pure recognition of block markers typed into a block-local source.
export module elmd.core.document_input_syntax;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.dialect;
import elmd.core.utf;

export namespace elmd::document_input_rules::detail {

inline bool horizontal_space(char32_t value) {
    return value == U' ' || value == U'\t';
}

inline std::size_t leading_spaces(std::u32string_view value) {
    std::size_t count = 0;
    while (count < value.size() && value[count] == U' ' && count < 4) ++count;
    return count;
}

struct MarkerMatch {
    enum class Kind { Heading, Quote, BulletList, OrderedList, TaskList } kind;
    std::size_t length = 0;
    std::uint8_t heading_level = 0;
    std::uint64_t list_start = 1;
    char32_t list_delimiter = U'.';
    bool checked = false;
};

inline std::optional<MarkerMatch> recognize_marker(std::u32string_view source, std::size_t caret) {
    if (caret == 0 || caret > source.size()) return std::nullopt;
    auto prefix = source.substr(0, caret);
    const auto indent = leading_spaces(prefix);
    if (indent > 3) return std::nullopt;
    auto cursor = indent;

    if (cursor < prefix.size() && prefix[cursor] == U'#') {
        const auto marker_start = cursor;
        while (cursor < prefix.size() && prefix[cursor] == U'#') ++cursor;
        const auto level = cursor - marker_start;
        if (level >= 1 && level <= 6 && cursor + 1 == prefix.size() && horizontal_space(prefix[cursor])) {
            return MarkerMatch{MarkerMatch::Kind::Heading, caret, static_cast<std::uint8_t>(level)};
        }
    }

    cursor = indent;
    if (cursor + 2 == prefix.size() && prefix[cursor] == U'>' && horizontal_space(prefix[cursor + 1])) {
        return MarkerMatch{MarkerMatch::Kind::Quote, caret};
    }

    cursor = indent;
    if (cursor < prefix.size() && (prefix[cursor] == U'-' || prefix[cursor] == U'+' || prefix[cursor] == U'*')) {
        ++cursor;
        if (cursor >= prefix.size() || !horizontal_space(prefix[cursor])) return std::nullopt;
        while (cursor < prefix.size() && horizontal_space(prefix[cursor])) ++cursor;
        if (cursor + 4 == prefix.size() && prefix[cursor] == U'['
            && (prefix[cursor + 1] == U' ' || prefix[cursor + 1] == U'x' || prefix[cursor + 1] == U'X')
            && prefix[cursor + 2] == U']' && horizontal_space(prefix[cursor + 3])) {
            return MarkerMatch{
                MarkerMatch::Kind::TaskList,
                caret,
                0,
                1,
                U'.',
                prefix[cursor + 1] == U'x' || prefix[cursor + 1] == U'X'};
        }
        if (cursor == prefix.size()) return MarkerMatch{MarkerMatch::Kind::BulletList, caret};
        return std::nullopt;
    }

    cursor = indent;
    const auto number_start = cursor;
    std::uint64_t number = 0;
    while (cursor < prefix.size() && prefix[cursor] >= U'0' && prefix[cursor] <= U'9'
        && cursor - number_start < 9) {
        number = number * 10 + static_cast<std::uint64_t>(prefix[cursor] - U'0');
        ++cursor;
    }
    if (cursor > number_start && cursor - number_start <= 9 && cursor + 2 == prefix.size()
        && (prefix[cursor] == U'.' || prefix[cursor] == U')') && horizontal_space(prefix[cursor + 1])) {
        return MarkerMatch{MarkerMatch::Kind::OrderedList, caret, 0, number, prefix[cursor]};
    }
    return std::nullopt;
}

inline std::u32string trim_horizontal(std::u32string_view value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && horizontal_space(value[begin])) ++begin;
    while (end > begin && horizontal_space(value[end - 1])) --end;
    return std::u32string(value.substr(begin, end - begin));
}

struct FenceMatch {
    char32_t marker = U'`';
    std::size_t count = 0;
    std::u32string info;
    std::u32string exact;
};

struct RawBlockOpening {
    BlockKind block_kind = BlockKind::CodeBlock;
    BlockSourceKind source_kind = BlockSourceKind::FencedCode;
    MathDelimiter math_delimiter = MathDelimiter::BlockDollar;
    std::u32string source;
};

inline std::optional<FenceMatch> recognize_fence(std::u32string_view source) {
    const auto indent = leading_spaces(source);
    if (indent > 3 || indent >= source.size()) return std::nullopt;
    const auto marker = source[indent];
    if (marker != U'`' && marker != U'~') return std::nullopt;
    auto cursor = indent;
    while (cursor < source.size() && source[cursor] == marker) ++cursor;
    const auto count = cursor - indent;
    if (count < 3) return std::nullopt;
    auto info = trim_horizontal(source.substr(cursor));
    if (marker == U'`' && info.find(U'`') != std::u32string::npos) return std::nullopt;
    return FenceMatch{marker, count, std::move(info), std::u32string(source)};
}

inline std::optional<RawBlockOpening> recognize_raw_block_opening(
    std::u32string_view source,
    const MarkdownDialect& dialect) {
    if (auto fence = recognize_fence(source)) {
        auto info = cps_to_utf8(fence->info);
        const auto indent = leading_spaces(source);
        auto closing = std::u32string(source.substr(0, indent))
            + std::u32string(fence->count, fence->marker);
        RawBlockOpening opening;
        opening.source = fence->exact + U"\n" + closing;
        if (info == "math" && dialect.math.fenced_math) {
            opening.block_kind = BlockKind::MathBlock;
            opening.source_kind = BlockSourceKind::FencedMath;
            opening.math_delimiter = MathDelimiter::FencedMath;
        }
        return opening;
    }

    const auto indent = leading_spaces(source);
    if (indent > 3) return std::nullopt;
    const auto exact = std::u32string(source);
    const auto marker = trim_horizontal(source.substr(indent));
    RawBlockOpening opening;
    opening.block_kind = BlockKind::MathBlock;
    if (marker == U"$$" && dialect.math.block_dollar) {
        opening.source_kind = BlockSourceKind::DollarMath;
        opening.math_delimiter = MathDelimiter::BlockDollar;
        opening.source = exact + U"\n" + std::u32string(source.substr(0, indent)) + U"$$";
        return opening;
    }
    if (marker == U"\\[" && dialect.math.block_bracket) {
        opening.source_kind = BlockSourceKind::BracketMath;
        opening.math_delimiter = MathDelimiter::BlockBracket;
        opening.source = exact + U"\n" + std::u32string(source.substr(0, indent)) + U"\\]";
        return opening;
    }
    return std::nullopt;
}

} // namespace elmd::document_input_rules::detail


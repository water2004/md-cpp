// folia.core.block_source — lossless, block-local source documents for raw
// code and display-math blocks. The source is authoritative; the tree is a
// projection used for rendering, syntax metadata, and semantic Enter rules.
export module folia.core.block_source;
import std;
import folia.core.text_edit;
import folia.core.utf;

export namespace folia {

enum class BlockSourceKind {
    FencedCode,
    IndentedCode,
    HtmlCode,
    DollarMath,
    BracketMath,
    FencedMath,
};

enum class BlockSourceTokenKind {
    OpeningMarker,
    InfoString,
    Indentation,
    Content,
    LineBreak,
    ClosingMarker,
    Error,
};

struct BlockSourceToken {
    BlockSourceTokenKind kind = BlockSourceTokenKind::Error;
    SourceRange source_range;

    bool operator==(const BlockSourceToken&) const = default;
};

struct BlockSourceTree {
    BlockSourceKind kind = BlockSourceKind::FencedCode;
    std::vector<BlockSourceToken> tokens;
    std::optional<SourceRange> info_range;
    std::vector<SourceRange> content_ranges;
    std::vector<std::size_t> content_to_source;
    std::u32string content;
    std::optional<std::string> language;
    bool complete_opening = false;
    bool complete_closing = false;
};

struct BlockSourceStorage {
    std::u32string source;
    BlockSourceTree tree;
};

struct BlockSourceDocument {
    std::unique_ptr<BlockSourceStorage> storage;

    BlockSourceDocument() = default;
    BlockSourceDocument(std::u32string source, BlockSourceTree tree)
        : storage(std::make_unique<BlockSourceStorage>(std::move(source), std::move(tree))) {}
    BlockSourceDocument(BlockSourceDocument const& other)
        : storage(other.storage ? std::make_unique<BlockSourceStorage>(*other.storage) : nullptr) {}
    BlockSourceDocument& operator=(BlockSourceDocument const& other) {
        if (this != &other) storage = other.storage ? std::make_unique<BlockSourceStorage>(*other.storage) : nullptr;
        return *this;
    }
    BlockSourceDocument(BlockSourceDocument&&) noexcept = default;
    BlockSourceDocument& operator=(BlockSourceDocument&&) noexcept = default;

    std::u32string const& source() const {
        static const std::u32string empty;
        return storage ? storage->source : empty;
    }
    std::u32string& source() {
        return ensure_storage().source;
    }
    BlockSourceTree const& tree() const {
        static const BlockSourceTree empty;
        return storage ? storage->tree : empty;
    }
    BlockSourceTree& tree() {
        return ensure_storage().tree;
    }
    bool empty() const { return !storage; }

private:
    BlockSourceStorage& ensure_storage() {
        if (!storage) storage = std::make_unique<BlockSourceStorage>();
        return *storage;
    }
};

namespace block_source_detail {

inline bool horizontal_space(char32_t value) {
    return value == U' ' || value == U'\t';
}

inline std::size_t line_end(std::u32string_view source, std::size_t start) {
    while (start < source.size() && source[start] != U'\r' && source[start] != U'\n') ++start;
    return start;
}

inline std::size_t line_ending_length(std::u32string_view source, std::size_t at) {
    if (at >= source.size()) return 0;
    if (source[at] == U'\r') {
        return at + 1 < source.size() && source[at + 1] == U'\n' ? 2u : 1u;
    }
    return source[at] == U'\n' ? 1u : 0u;
}

inline void push_token(BlockSourceTree& tree, BlockSourceTokenKind kind, std::size_t start, std::size_t end) {
    if (start < end) tree.tokens.push_back({kind, {start, end}});
}

inline std::u32string trim_horizontal(std::u32string_view value) {
    std::size_t start = 0;
    std::size_t end = value.size();
    while (start < end && horizontal_space(value[start])) ++start;
    while (end > start && horizontal_space(value[end - 1])) --end;
    return std::u32string(value.substr(start, end - start));
}

inline std::optional<SourceRange> closing_fence(
    std::u32string_view source,
    std::size_t content_start,
    char32_t marker,
    std::size_t minimum) {
    auto line_start = content_start;
    while (line_start <= source.size()) {
        const auto end = line_end(source, line_start);
        auto cursor = line_start;
        std::size_t indent = 0;
        while (cursor < end && source[cursor] == U' ' && indent < 4) {
            ++cursor;
            ++indent;
        }
        if (indent <= 3) {
            const auto marker_start = cursor;
            while (cursor < end && source[cursor] == marker) ++cursor;
            const auto count = cursor - marker_start;
            while (cursor < end && horizontal_space(source[cursor])) ++cursor;
            if (count >= minimum && cursor == end) return SourceRange{line_start, end};
        }
        const auto ending_length = line_ending_length(source, end);
        if (ending_length == 0) break;
        line_start = end + ending_length;
    }
    return std::nullopt;
}

inline void identity_content_map(BlockSourceTree& tree, SourceRange range) {
    tree.content_ranges.clear();
    if (!range.empty()) tree.content_ranges.push_back(range);
    tree.content.clear();
    tree.content_to_source.reserve(range.length() + 1);
    for (auto offset = range.start; offset <= range.end; ++offset) {
        tree.content_to_source.push_back(offset);
    }
}

inline BlockSourceTree parse_fenced(std::u32string_view source, BlockSourceKind kind) {
    BlockSourceTree tree;
    tree.kind = kind;
    const auto first_end = line_end(source, 0);
    auto cursor = std::size_t{0};
    std::size_t indent = 0;
    while (cursor < first_end && source[cursor] == U' ' && indent < 4) {
        ++cursor;
        ++indent;
    }
    const auto marker_start = cursor;
    const auto marker = cursor < first_end ? source[cursor] : U'\0';
    while (cursor < first_end && source[cursor] == marker) ++cursor;
    const auto marker_count = cursor - marker_start;
    const auto valid_marker = indent <= 3
        && (marker == U'`' || marker == U'~')
        && marker_count >= 3;
    if (!valid_marker) {
        push_token(tree, BlockSourceTokenKind::Error, 0, source.size());
        identity_content_map(tree, {0, source.size()});
        tree.content = std::u32string(source);
        return tree;
    }

    const auto opening_line_ending_length = line_ending_length(source, first_end);
    tree.complete_opening = opening_line_ending_length != 0;
    push_token(tree, BlockSourceTokenKind::OpeningMarker, 0, cursor);
    tree.info_range = SourceRange{cursor, first_end};
    push_token(tree, BlockSourceTokenKind::InfoString, cursor, first_end);
    const auto info = trim_horizontal(source.substr(cursor, first_end - cursor));
    if (!info.empty()) tree.language = cps_to_utf8(info);
    auto content_start = first_end;
    if (opening_line_ending_length != 0) {
        push_token(
            tree,
            BlockSourceTokenKind::LineBreak,
            first_end,
            first_end + opening_line_ending_length);
        content_start = first_end + opening_line_ending_length;
    }

    const auto closing = closing_fence(source, content_start, marker, marker_count);
    const auto content_end = closing ? closing->start : source.size();
    push_token(tree, BlockSourceTokenKind::Content, content_start, content_end);
    identity_content_map(tree, {content_start, content_end});
    tree.content = std::u32string(source.substr(content_start, content_end - content_start));
    if (closing) {
        tree.complete_closing = true;
        push_token(tree, BlockSourceTokenKind::ClosingMarker, closing->start, closing->end);
        if (closing->end < source.size()) {
            push_token(tree, BlockSourceTokenKind::Error, closing->end, source.size());
        }
    }
    return tree;
}

inline BlockSourceTree parse_math(std::u32string_view source, BlockSourceKind kind) {
    BlockSourceTree tree;
    tree.kind = kind;
    const auto opening = kind == BlockSourceKind::DollarMath
        ? std::u32string_view{U"$$"}
        : std::u32string_view{U"\\["};
    const auto closing_text = kind == BlockSourceKind::DollarMath
        ? std::u32string_view{U"$$"}
        : std::u32string_view{U"\\]"};
    auto cursor = std::size_t{0};
    std::size_t indent = 0;
    while (cursor < source.size() && source[cursor] == U' ' && indent < 4) {
        ++cursor;
        ++indent;
    }
    const auto has_marker = indent <= 3
        && source.substr(cursor, opening.size()) == opening;
    if (!has_marker) {
        push_token(tree, BlockSourceTokenKind::Error, 0, source.size());
        identity_content_map(tree, {0, source.size()});
        tree.content = std::u32string(source);
        return tree;
    }
    cursor += opening.size();
    tree.complete_opening = true;
    push_token(tree, BlockSourceTokenKind::OpeningMarker, 0, cursor);
    auto content_start = cursor;
    const auto opening_line_ending_length = line_ending_length(source, cursor);
    if (opening_line_ending_length != 0) {
        push_token(
            tree,
            BlockSourceTokenKind::LineBreak,
            cursor,
            cursor + opening_line_ending_length);
        content_start = cursor + opening_line_ending_length;
    }

    std::optional<SourceRange> closing;
    const auto closing_start = source.find(closing_text, content_start);
    if (closing_start != std::u32string_view::npos) {
        closing = SourceRange{closing_start, closing_start + closing_text.size()};
    }
    const auto content_end = closing ? closing->start : source.size();
    push_token(tree, BlockSourceTokenKind::Content, content_start, content_end);
    identity_content_map(tree, {content_start, content_end});
    tree.content = std::u32string(source.substr(content_start, content_end - content_start));
    if (closing) {
        tree.complete_closing = true;
        push_token(tree, BlockSourceTokenKind::ClosingMarker, closing->start, closing->end);
        if (closing->end < source.size()) {
            push_token(tree, BlockSourceTokenKind::Error, closing->end, source.size());
        }
    }
    return tree;
}

inline BlockSourceTree parse_indented(std::u32string_view source) {
    BlockSourceTree tree;
    tree.kind = BlockSourceKind::IndentedCode;
    tree.complete_opening = true;
    tree.complete_closing = true;
    auto line_start = std::size_t{0};
    while (line_start < source.size()) {
        const auto end = line_end(source, line_start);
        auto content_start = line_start;
        if (source[content_start] == U'\t') {
            push_token(tree, BlockSourceTokenKind::Indentation, content_start, content_start + 1);
            ++content_start;
        } else {
            auto spaces = std::size_t{0};
            while (content_start < end && source[content_start] == U' ' && spaces < 4) {
                ++content_start;
                ++spaces;
            }
            if (spaces == 4) {
                push_token(tree, BlockSourceTokenKind::Indentation, line_start, content_start);
            } else if (line_start != end) {
                content_start = line_start;
            }
        }
        if (content_start < end) {
            tree.content_ranges.push_back({content_start, end});
            push_token(tree, BlockSourceTokenKind::Content, content_start, end);
            for (auto offset = content_start; offset < end; ++offset) {
                tree.content.push_back(source[offset]);
                tree.content_to_source.push_back(offset);
            }
        }
        const auto ending_length = line_ending_length(source, end);
        if (ending_length != 0) {
            push_token(tree, BlockSourceTokenKind::LineBreak, end, end + ending_length);
            tree.content.push_back(U'\n');
            tree.content_to_source.push_back(end);
            line_start = end + ending_length;
        } else {
            line_start = end;
        }
    }
    tree.content_to_source.push_back(source.size());
    return tree;
}

inline BlockSourceTree parse_html_code(std::u32string_view source) {
    BlockSourceTree tree;
    tree.kind = BlockSourceKind::HtmlCode;
    auto opening_end = source.find(U'>');
    auto closing_start = source.rfind(U"</pre");
    if (opening_end == std::u32string_view::npos
        || closing_start == std::u32string_view::npos
        || closing_start <= opening_end) {
        push_token(tree, BlockSourceTokenKind::Error, 0, source.size());
        identity_content_map(tree, {0, source.size()});
        tree.content = std::u32string(source);
        return tree;
    }
    ++opening_end;
    auto content_start = opening_end;
    auto content_end = closing_start;
    if (source.substr(content_start, 5) == U"<code") {
        const auto code_open_end = source.find(U'>', content_start);
        const auto code_close = source.rfind(U"</code", content_end);
        if (code_open_end != std::u32string_view::npos
            && code_close != std::u32string_view::npos
            && code_close > code_open_end) {
            content_start = code_open_end + 1;
            content_end = code_close;
        }
    }
    tree.complete_opening = true;
    tree.complete_closing = true;
    push_token(tree, BlockSourceTokenKind::OpeningMarker, 0, content_start);
    push_token(tree, BlockSourceTokenKind::Content, content_start, content_end);
    push_token(tree, BlockSourceTokenKind::ClosingMarker, content_end, source.size());
    if (content_start < content_end) tree.content_ranges.push_back({content_start, content_end});

    for (auto cursor = content_start; cursor < content_end;) {
        if (source[cursor] == U'<') {
            const auto tag_end = source.find(U'>', cursor + 1);
            if (tag_end != std::u32string_view::npos && tag_end < content_end) {
                auto tag = source.substr(cursor + 1, tag_end - cursor - 1);
                if (tag == U"br" || tag == U"br/" || tag == U"/p" || tag == U"/div") {
                    tree.content.push_back(U'\n');
                    tree.content_to_source.push_back(cursor);
                }
                cursor = tag_end + 1;
                continue;
            }
        }
        if (source[cursor] == U'&') {
            const auto semicolon = source.find(U';', cursor + 1);
            if (semicolon != std::u32string_view::npos && semicolon < content_end
                && semicolon - cursor <= 10) {
                const auto entity = source.substr(cursor + 1, semicolon - cursor - 1);
                std::optional<char32_t> decoded;
                if (entity == U"amp") decoded = U'&';
                else if (entity == U"lt") decoded = U'<';
                else if (entity == U"gt") decoded = U'>';
                else if (entity == U"quot") decoded = U'"';
                else if (entity == U"apos" || entity == U"#39") decoded = U'\'';
                else if (entity == U"nbsp") decoded = U' ';
                if (decoded) {
                    tree.content.push_back(*decoded);
                    tree.content_to_source.push_back(cursor);
                    cursor = semicolon + 1;
                    continue;
                }
            }
        }
        tree.content.push_back(source[cursor]);
        tree.content_to_source.push_back(cursor);
        ++cursor;
    }
    tree.content_to_source.push_back(content_end);
    return tree;
}

} // namespace block_source_detail

inline BlockSourceTree parse_block_source(std::u32string_view source, BlockSourceKind kind) {
    switch (kind) {
        case BlockSourceKind::FencedCode:
        case BlockSourceKind::FencedMath:
            return block_source_detail::parse_fenced(source, kind);
        case BlockSourceKind::IndentedCode:
            return block_source_detail::parse_indented(source);
        case BlockSourceKind::HtmlCode:
            return block_source_detail::parse_html_code(source);
        case BlockSourceKind::DollarMath:
        case BlockSourceKind::BracketMath:
            return block_source_detail::parse_math(source, kind);
    }
    return {};
}

inline BlockSourceDocument make_block_source(std::u32string source, BlockSourceKind kind) {
    BlockSourceDocument document;
    document.source() = std::move(source);
    document.tree() = parse_block_source(document.source(), kind);
    return document;
}

inline void reparse_block_source(BlockSourceDocument& document) {
    document.tree() = parse_block_source(document.source(), document.tree().kind);
}

inline bool block_source_tokens_partition(const BlockSourceDocument& document) {
    std::size_t cursor = 0;
    for (const auto& token : document.tree().tokens) {
        if (token.source_range.start != cursor
            || !token.source_range.valid_for(document.source().size())) return false;
        cursor = token.source_range.end;
    }
    return cursor == document.source().size();
}

inline std::u32string flatten_block_source_tokens(const BlockSourceDocument& document) {
    std::u32string result;
    for (const auto& token : document.tree().tokens) {
        result.append(
            document.source(),
            token.source_range.start,
            token.source_range.length());
    }
    return result;
}

inline std::u32string block_source_content(const BlockSourceDocument& document) {
    return document.tree().content;
}

inline std::size_t block_source_offset_for_content(
    const BlockSourceDocument& document,
    std::size_t content_offset) {
    if (document.tree().content_to_source.empty()) return 0;
    const auto index = (std::min)(content_offset, document.tree().content_to_source.size() - 1);
    return document.tree().content_to_source[index];
}

} // namespace folia

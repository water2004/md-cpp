// elmd.core.parser — Markdown parser. Pure core, no Windows dependency.
//
// Faithful C++ port of the Rust `markdown-parse` crate with the bug fixes
// dictated by HANDOFF.md applied from the start:
//   * char-indexed slicing everywhere (no byte-slice-of-char-idx panics)
//   * strong requires TWO `*`/`_`; emphasis a single one (the old
//     `**word**` misread-as-strong bug never reproduced)
//   * `##no-space` is not a heading
//   * `rest_of_line()` is read-only; content consumers advance_n(len) to
//     avoid the list-reparse-as-paragraph duplication bug
//   * `$$` at line start is block math; a lone `$` immediately followed by
//     `$` is NOT inline math — left to the block path
//   * unsafe raw HTML becomes BlockNode::UnsupportedMarkup
//   * ` ```math ` fence becomes MathBlock{ FencedMath } (when enabled)
//   * list / blockquote recursion uses parse_blocks with stop conditions
//
// All offsets are CHAR indices into a Vec<char32> snapshot.
export module elmd.core.parser;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.image_dimension;
import elmd.core.diagnostics;
import elmd.core.metadata;
import elmd.core.symbols;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.block_line_recognizer;
import elmd.core.callout;
import elmd.core.document;
import elmd.core.document_symbols;
import elmd.core.outline;
import elmd.core.slug;
import elmd.core.utf;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.html_cst;
import elmd.core.instrumentation;
import elmd.core.serializer;
import elmd.core.text_edit;

export namespace elmd {

using ParseProgressCallback = std::function<void(std::size_t consumed, std::size_t total)>;

struct ParseInput {
    std::uint64_t revision = 1;
    std::string text;                       // UTF-8 source
    MarkdownDialect dialect = default_dialect();
    ParseProgressCallback progress;

    ParseInput() = default;
    ParseInput(
        std::uint64_t rev,
        std::string t,
        MarkdownDialect d = default_dialect(),
        ParseProgressCallback callback = {})
        : revision(rev), text(std::move(t)), dialect(d), progress(std::move(callback)) {}
};

struct ParseOutput {
    std::uint64_t revision = 1;
    EditorDocument document;
    DocumentSymbolIndex symbols;
    DocumentSymbolContributions symbol_contributions;
    Outline outline;
    std::vector<Diagnostic> diagnostics;
};

// A block fragment is the smallest parser entry point used by structure-aware
// editing. Its physical ranges are transient mapping data for replacing one
// edited direct block; they never become a second selection coordinate.
struct BlockFragmentSourceRange {
    NodeId node_id{};
    SourceRange source_range;
    SourceRange content_range;
};

struct ParsedBlockFragment {
    BlockVec blocks;
    std::vector<BlockFragmentSourceRange> source_ranges;
};

// ---------------------------------------------------------------------------
//                              the Parser
// ---------------------------------------------------------------------------
namespace detail {

// Parse-time physical ranges are transient recognizer bookkeeping only. They
// are never stored in EditorDocument and never become an editing coordinate.
struct PhysicalRange {
    std::size_t start = 0;
    std::size_t end = 0;
};

struct ParserSourceRange {
    NodeId node_id{};
    PhysicalRange source_range;
    PhysicalRange content_range;
    std::vector<PhysicalRange> marker_ranges;

    ParserSourceRange() = default;
    ParserSourceRange(NodeId id, PhysicalRange source, PhysicalRange content)
        : node_id(id), source_range(source), content_range(content) {}
};

class Parser {
public:
    struct LinkDefinition {
        std::string label;
        std::string href;
        std::optional<std::string> title;
        std::size_t source_start = 0;
        std::size_t source_end = 0;
    };

    struct HtmlTag {
        std::string name;
        std::unordered_map<std::string, std::string> attributes;
        std::size_t start = 0;
        std::size_t end = 0;
        bool closing = false;
        bool self_closing = false;
    };

    const ParseInput* input;
    std::u32string cps;          // text as codepoints (this is the index space)
    std::size_t pos = 0;
    std::uint64_t node_counter = 1;
    std::vector<Diagnostic> diagnostics;
    std::vector<ParserSourceRange> source_ranges;
    std::unordered_map<std::string, LinkDefinition> link_definitions;
    std::unordered_map<std::size_t, LinkDefinition> link_definitions_by_start;

    explicit Parser(const ParseInput* in) : input(in) {
        cps = utf8_to_cps(in->text);
        scan_link_definitions();
    }

    static std::string normalize_link_label(std::u32string_view label) {
        std::u32string normalized;
        bool pending_space = false;
        for (auto ch : label) {
            if (ch == U' ' || ch == U'\t' || ch == U'\n' || ch == U'\r') {
                if (!normalized.empty()) pending_space = true;
                continue;
            }
            if (pending_space) normalized.push_back(U' ');
            pending_space = false;
            if (ch >= U'A' && ch <= U'Z') ch = ch - U'A' + U'a';
            normalized.push_back(ch);
        }
        return cps_to_utf8(normalized);
    }

    std::optional<LinkDefinition> link_definition_at(std::size_t start) const {
        const auto line_end = line_content_end(start);
        std::size_t cursor = start;
        std::size_t spaces = 0;
        while (cursor < line_end && cps[cursor] == U' ' && spaces < 4) { ++cursor; ++spaces; }
        if (spaces > 3 || cursor >= line_end || cps[cursor] != U'[') return std::nullopt;
        auto label_start = ++cursor;
        while (cursor < line_end && cps[cursor] != U']') ++cursor;
        if (cursor == label_start || cursor >= line_end || cursor + 1 >= line_end || cps[cursor + 1] != U':') return std::nullopt;
        auto label_end = cursor;
        cursor += 2;
        if (cursor >= line_end || (cps[cursor] != U' ' && cps[cursor] != U'\t')) return std::nullopt;
        while (cursor < line_end && (cps[cursor] == U' ' || cps[cursor] == U'\t')) ++cursor;
        std::u32string href;
        if (cursor < line_end && cps[cursor] == U'<') {
            ++cursor;
            while (cursor < line_end && cps[cursor] != U'>') href.push_back(cps[cursor++]);
            if (cursor >= line_end || cps[cursor] != U'>') return std::nullopt;
            ++cursor;
        } else {
            while (cursor < line_end && cps[cursor] != U' ' && cps[cursor] != U'\t') href.push_back(cps[cursor++]);
        }
        if (href.empty()) return std::nullopt;
        while (cursor < line_end && (cps[cursor] == U' ' || cps[cursor] == U'\t')) ++cursor;
        std::optional<std::string> title;
        if (cursor < line_end) {
            auto opening = cps[cursor];
            auto closing = opening == U'(' ? U')' : opening;
            if (opening != U'"' && opening != U'\'' && opening != U'(') return std::nullopt;
            ++cursor;
            std::u32string value;
            while (cursor < line_end && cps[cursor] != closing) value.push_back(cps[cursor++]);
            if (cursor >= line_end || cps[cursor] != closing) return std::nullopt;
            ++cursor;
            while (cursor < line_end && (cps[cursor] == U' ' || cps[cursor] == U'\t')) ++cursor;
            if (cursor != line_end) return std::nullopt;
            title = cps_to_utf8(value);
        }
        LinkDefinition definition;
        definition.label = normalize_link_label(std::u32string_view(cps).substr(label_start, label_end - label_start));
        definition.href = cps_to_utf8(href);
        definition.title = std::move(title);
        definition.source_start = start;
        definition.source_end = next_line_start(line_end);
        return definition;
    }

    InlineParseContext inline_parse_context() {
        InlineParseContext context;
        context.dialect = input->dialect;
        context.allocate_id = [this] { return next_node_id(); };
        context.resolve_link_label = [this](const std::string& label) -> std::optional<InlineLinkDef> {
            const auto found = link_definitions.find(label);
            if (found == link_definitions.end()) return std::nullopt;
            return InlineLinkDef{found->second.href, found->second.title};
        };
        return context;
    }

    InlineDocument make_inline_document(
        std::size_t start,
        std::size_t end,
        InlineSyntaxMode syntax_mode = InlineSyntaxMode::Markdown) {
        start = (std::min)(start, cps.size());
        end = (std::min)((std::max)(end, start), cps.size());
        InlineDocument document;
        document.source = cps.substr(start, end - start);
        document.syntax_mode = syntax_mode;
        reparse_inline_document(document, inline_parse_context());
        return document;
    }

    void scan_link_definitions() {
        std::size_t line_start = 0;
        while (line_start < cps.size()) {
            if (auto definition = link_definition_at(line_start)) {
                link_definitions.try_emplace(definition->label, *definition);
                link_definitions_by_start.emplace(line_start, *definition);
            }
            line_start = line_content_end(line_start);
            line_start += line_ending_length(line_start);
        }
    }

    std::optional<HtmlTag> html_tag_at(std::size_t at, std::size_t limit) const {
        if (at >= limit || cps[at] != U'<') return std::nullopt;
        HtmlTag tag;
        tag.start = at;
        auto cursor = at + 1;
        if (cursor < limit && cps[cursor] == U'/') { tag.closing = true; ++cursor; }
        auto name_start = cursor;
        while (cursor < limit && (is_alnum_(cps[cursor]) || cps[cursor] == U'-')) ++cursor;
        if (cursor == name_start) return std::nullopt;
        tag.name = cps_to_utf8(std::u32string_view(cps).substr(name_start, cursor - name_start));
        std::transform(tag.name.begin(), tag.name.end(), tag.name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        while (cursor < limit) {
            while (cursor < limit && (cps[cursor] == U' ' || cps[cursor] == U'\t')) ++cursor;
            if (cursor >= limit || is_line_ending_character(cps[cursor])) return std::nullopt;
            if (cps[cursor] == U'>') { tag.end = cursor + 1; return tag; }
            if (cps[cursor] == U'/' && cursor + 1 < limit && cps[cursor + 1] == U'>') {
                tag.self_closing = true;
                tag.end = cursor + 2;
                return tag;
            }
            auto attribute_start = cursor;
            while (cursor < limit && (is_alnum_(cps[cursor]) || cps[cursor] == U'-' || cps[cursor] == U'_')) ++cursor;
            if (cursor == attribute_start) return std::nullopt;
            auto attribute = cps_to_utf8(std::u32string_view(cps).substr(attribute_start, cursor - attribute_start));
            std::transform(attribute.begin(), attribute.end(), attribute.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            while (cursor < limit && (cps[cursor] == U' ' || cps[cursor] == U'\t')) ++cursor;
            std::string value;
            if (cursor < limit && cps[cursor] == U'=') {
                ++cursor;
                while (cursor < limit && (cps[cursor] == U' ' || cps[cursor] == U'\t')) ++cursor;
                if (cursor >= limit) return std::nullopt;
                if (cps[cursor] == U'"' || cps[cursor] == U'\'') {
                    auto quote = cps[cursor++];
                    auto value_start = cursor;
                    while (cursor < limit && cps[cursor] != quote
                        && !is_line_ending_character(cps[cursor])) ++cursor;
                    if (cursor >= limit || cps[cursor] != quote) return std::nullopt;
                    value = cps_to_utf8(std::u32string_view(cps).substr(value_start, cursor - value_start));
                    ++cursor;
                } else {
                    auto value_start = cursor;
                    while (cursor < limit && cps[cursor] != U' ' && cps[cursor] != U'\t' && cps[cursor] != U'>') ++cursor;
                    value = cps_to_utf8(std::u32string_view(cps).substr(value_start, cursor - value_start));
                }
            }
            if (!attribute.starts_with("on") && attribute != "style") tag.attributes[attribute] = std::move(value);
        }
        return std::nullopt;
    }

    std::optional<std::pair<std::size_t, std::size_t>> html_closing_tag(std::string_view name, std::size_t from, std::size_t limit) const {
        std::size_t depth = 1;
        for (auto cursor = from; cursor < limit; ++cursor) {
            if (cps[cursor] != U'<') continue;
            auto tag = html_tag_at(cursor, limit);
            if (!tag || tag->name != name) continue;
            if (tag->closing) {
                if (--depth == 0) return std::pair{cursor, tag->end};
            } else if (!tag->self_closing) {
                ++depth;
            }
            cursor = tag->end - 1;
        }
        return std::nullopt;
    }

    static bool safe_link_target(std::string_view value, bool image) {
        std::size_t start = 0;
        std::size_t end = value.size();
        while (start < end && static_cast<unsigned char>(value[start]) <= 0x20) ++start;
        while (end > start && static_cast<unsigned char>(value[end - 1]) <= 0x20) --end;
        auto lower = std::string(value.substr(start, end - start));
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        auto colon = lower.find(':');
        auto boundary = lower.find_first_of("/?#");
        if (colon != std::string::npos && (boundary == std::string::npos || colon < boundary)) {
            auto scheme = lower.substr(0, colon);
            if (scheme == "http" || scheme == "https" || (!image && scheme == "mailto")) return true;
            return image && scheme == "data" && lower.starts_with("data:image/");
        }
        return true;
    }

    static bool ascii_punctuation(char32_t value) {
        return (value >= U'!' && value <= U'/') || (value >= U':' && value <= U'@') || (value >= U'[' && value <= U'`') || (value >= U'{' && value <= U'~');
    }

    static std::optional<float> html_dimension(HtmlTag const& tag, std::string_view name) {
        auto found = tag.attributes.find(std::string(name));
        if (found == tag.attributes.end()) return std::nullopt;
        auto value = found->second;
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        if (value.size() >= 2 && value.ends_with("px")) value.resize(value.size() - 2);
        try {
            auto parsed = std::stof(value);
            if (!std::isfinite(parsed) || parsed <= 0.0f) return std::nullopt;
            return (std::min)(parsed, 4096.0f);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<std::pair<std::u32string, std::size_t>> html_entity_at(std::size_t at, std::size_t limit) const {
        if (at >= limit || cps[at] != U'&') return std::nullopt;
        auto end = at + 1;
        while (end < limit && end - at <= 12 && cps[end] != U';'
            && !is_line_ending_character(cps[end])) ++end;
        if (end >= limit || cps[end] != U';') return std::nullopt;
        auto entity = cps_to_utf8(std::u32string_view(cps).substr(at + 1, end - at - 1));
        if (entity == "amp") return std::pair{std::u32string(1, U'&'), end + 1};
        if (entity == "lt") return std::pair{std::u32string(1, U'<'), end + 1};
        if (entity == "gt") return std::pair{std::u32string(1, U'>'), end + 1};
        if (entity == "quot") return std::pair{std::u32string(1, U'"'), end + 1};
        if (entity == "apos" || entity == "#39") return std::pair{std::u32string(1, U'\''), end + 1};
        if (entity == "nbsp") return std::pair{std::u32string(1, U' '), end + 1};
        if (entity.starts_with("#")) {
            try {
                auto hex = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
                auto value = std::stoul(entity.substr(hex ? 2 : 1), nullptr, hex ? 16 : 10);
                if (value <= 0x10ffff && !(0xd800 <= value && value <= 0xdfff)) return std::pair{std::u32string(1, static_cast<char32_t>(value)), end + 1};
            } catch (...) {}
        }
        return std::nullopt;
    }

    NodeId next_node_id() { return NodeId(node_counter++); }
    std::size_t cur() const { return std::size_t(pos); }
    bool eof() const { return pos >= cps.size(); }
    char32_t peek(std::size_t k = 0) const {
        return (pos + k < cps.size()) ? cps[pos + k] : 0;
    }
    // returns first of peek_n or 0
    char32_t peek1() const { return peek(0); }
    char32_t peek2(std::size_t k = 1) const { return peek(k); }
    void advance() { if (pos < cps.size()) ++pos; }
    void advance_n(std::size_t n) {
        pos = (pos + n > cps.size()) ? cps.size() : pos + n;
    }
    void report_progress() const {
        if (input->progress) input->progress((std::min)(pos, cps.size()), cps.size());
    }
    char32_t ch_at(std::size_t i) const { return (i < cps.size()) ? cps[i] : 0; }
    void skip_ws_inline() { while (peek1() == ' ' || peek1() == '\t') advance(); }
    BlockLineRecognizer line_recognizer() const { return BlockLineRecognizer(cps); }
    static bool is_line_ending_character(char32_t value) {
        return BlockLineRecognizer::is_line_ending_character(value);
    }
    std::size_t line_ending_length(std::size_t at) const {
        return line_recognizer().line_ending_length(at);
    }
    std::size_t line_content_end(std::size_t at) const {
        return line_recognizer().line_content_end(at);
    }
    bool consume_line_ending() {
        const auto length = line_ending_length(pos);
        if (length == 0) return false;
        advance_n(length);
        return true;
    }
    std::size_t next_line_start(std::size_t line_end) const {
        return line_recognizer().next_line_start(line_end);
    }
    bool is_line_start_at(std::size_t at) const {
        return line_recognizer().is_line_start_at(at);
    }
    bool peek_line_start() const {
        return is_line_start_at(pos);
    }
    bool is_blank_line() const {
        std::size_t i = pos;
        while (i < cps.size() && !is_line_ending_character(cps[i])) {
            if (cps[i] != ' ' && cps[i] != '\t') return false;
            ++i;
        }
        return true;
    }
    bool line_starts_fenced_code(std::size_t at) const {
        return line_recognizer().line_starts_fenced_code(at);
    }
    bool line_starts_fenced_code() const {
        return peek_line_start() && line_starts_fenced_code(pos);
    }
    bool line_is_thematic_break(std::size_t start, std::size_t* end = nullptr) const {
        return line_recognizer().line_is_thematic_break(start, end);
    }
    using SetextUnderline = BlockLineRecognizer::SetextUnderline;
    std::optional<SetextUnderline> setext_underline_at(std::size_t start) const {
        return line_recognizer().setext_underline_at(start);
    }
    bool line_starts_block_html(std::size_t at) const {
        return line_recognizer().line_starts_block_html(at);
    }
    bool line_starts_block_html() const {
        return peek_line_start() && line_starts_block_html(pos);
    }
    bool line_starts_interrupting_block(std::size_t at) const {
        return line_recognizer().line_starts_interrupting_block(at);
    }
    bool line_starts_interrupting_block() const {
        return peek_line_start() && line_starts_interrupting_block(pos);
    }
    // Read the current physical line without its CRLF/CR/LF terminator.
    std::pair<std::u32string, std::size_t> rest_of_line() const {
        const auto end = line_content_end(pos);
        return {std::u32string(std::u32string_view(cps).substr(pos, end - pos)), end - pos};
    }
    // Push a source range. idempotent.
    void push_range(NodeId id, PhysicalRange sr, PhysicalRange cr) {
        ParserSourceRange r(id, sr, cr);
        source_ranges.push_back(r);
    }
    // ---- block dispatch ----
    void parse_blank_lines(
        std::vector<BlockNode>& blocks,
        std::optional<std::size_t>& preceding_source_end) {
        std::vector<std::pair<std::size_t, std::size_t>> blank_ranges;
        while (!eof() && is_blank_line()) {
            const auto start = cur();
            while (peek1() == ' ' || peek1() == '\t') advance();
            consume_line_ending();
            blank_ranges.emplace_back(start, cur());
        }
        // A blank physical line between two non-empty blocks is Markdown's
        // block separator, not a third editable block.  Keep its exact bytes
        // in the following block's separator_before.  Additional blank lines
        // are genuine editable empty paragraphs.  At document boundaries no
        // following/preceding content absorbs a separator, so every physical
        // blank line keeps block identity.
        const auto first_editable_blank = !eof()
            && !blocks.empty()
            && preceding_source_end
            && !(blocks.back().kind == BlockKind::Paragraph
                && blocks.back().inline_content.source.empty())
            ? 1u
            : 0u;
        for (std::size_t index = first_editable_blank; index < blank_ranges.size(); ++index) {
            BlockNode paragraph;
            paragraph.id = next_node_id();
            paragraph.kind = BlockKind::Paragraph;
            if (!blocks.empty() && preceding_source_end) {
                paragraph.separator_before = std::u32string(
                    std::u32string_view(cps).substr(
                        *preceding_source_end,
                        blank_ranges[index].first - *preceding_source_end));
            }
            push_range(
                paragraph.id,
                PhysicalRange(blank_ranges[index].first, blank_ranges[index].second),
                PhysicalRange(blank_ranges[index].first, blank_ranges[index].first));
            blocks.push_back(std::move(paragraph));
            preceding_source_end = blank_ranges[index].first;
        }
    }

    std::vector<BlockNode> parse_blocks(std::function<bool(const std::u32string&)> stop = nullptr) {
        std::vector<BlockNode> blocks;
        std::optional<std::size_t> preceding_source_end;
        while (!eof()) {
            if (is_blank_line()) {
                parse_blank_lines(blocks, preceding_source_end);
                report_progress();
                continue;
            }
            if (stop) {
                auto [line, _] = rest_of_line();
                if (stop(line)) break;
            }
            const auto block_start = cur();
            if (auto b = parse_block()) {
                if (!blocks.empty() && preceding_source_end) {
                    b->separator_before = std::u32string(
                        std::u32string_view(cps).substr(
                            *preceding_source_end,
                            block_start - *preceding_source_end));
                }
                auto block_end = cur();
                const auto serialized = serializer_detail::serialize_block(*b);
                if (block_end > block_start
                    && is_line_ending_character(cps[block_end - 1])
                    && (serialized.text.empty()
                        || !is_line_ending_character(serialized.text.back()))) {
                    --block_end;
                    if (block_end > block_start
                        && cps[block_end] == U'\n'
                        && cps[block_end - 1] == U'\r') --block_end;
                }
                blocks.push_back(std::move(*b));
                preceding_source_end = block_end;
            } else {
                advance();
            }
            report_progress();
        }
        return blocks;
    }

    bool is_callout_marker_line(const std::u32string& line) const {
        // detect "> [!KIND]" already-without-">"? used to stop block body
        // we just strip leading whitespace
        std::size_t i = 0; while (i < line.size() && (line[i]==' '||line[i]=='\t')) ++i;
        return false;
    }

    std::optional<BlockNode> parse_block() {
        // The dispatch order matches HANDOFF closely.
        bool ls = peek_line_start();
        if (ls) {
            if (auto b = try_parse_frontmatter())        return b;
            if (peek1() == '[' && ch_at(pos+1) == '^') {
                if (auto b = try_parse_footnote_definition()) return b;
            }
            if (auto b = try_parse_link_definition())    return b;
            if (peek1() == '#') {
                if (auto b = parse_heading()) return b;
            }
            if (peek1() == '>') {
                if (auto b = parse_blockquote_or_callout()) return b;
            }
            if (auto b = try_parse_indented_code_block()) return b;
            if (auto b = try_parse_code_block())         return b;
            if (auto b = try_parse_math_block())         return b;
            if (auto b = try_parse_image_block())        return b;
            if (auto b = try_parse_table())              return b;
            if (auto b = try_parse_thematic_break())     return b;
            if (auto b = try_parse_list_or_task())       return b;
            if (try_match_toc())                          return parse_toc();
        }
        if (peek1() == '<') {
            if (auto b = try_parse_raw_html_block())     return b;
        }
        return parse_paragraph();
    }

    // ---- frontmatter ----
    std::optional<BlockNode> try_parse_frontmatter() {
        if (pos != 0) return std::nullopt;
        auto [opening_line, opening_length] = rest_of_line();
        auto opening = trim_utf8(cps_to_utf8(opening_line));
        FrontmatterFormat fmt = FrontmatterFormat::Yaml;
        if (opening == "---") fmt = FrontmatterFormat::Yaml;
        else if (opening == "---toml" || opening == "--- toml") fmt = FrontmatterFormat::Toml;
        else if (opening == "---json" || opening == "--- json") fmt = FrontmatterFormat::Json;
        else return std::nullopt;
        std::size_t save = pos;
        advance_n(opening_length);
        consume_line_ending();
        while (true) {
            if (eof()) { pos = save; return std::nullopt; }
            auto [closing_line, closing_length] = rest_of_line();
            if (peek_line_start() && trim_utf8(cps_to_utf8(closing_line)) == "---") {
                std::size_t closing_start = pos;
                advance_n(closing_length);
                consume_line_ending();
                std::size_t content_end = closing_start;
                // slice by CHAR indices into UTF8 — safe for CJK
                std::size_t b1 = char_to_byte_in_input_(save), b2 = char_to_byte_in_input_(save + 3);
                std::size_t eC = char_to_byte_in_input_(content_end);
                std::string inner = input->text.substr(b2, (b1 < eC ? eC : b1) - b2);
                NodeId id = next_node_id();
                push_range(id, PhysicalRange(std::size_t(save), std::size_t(pos)), PhysicalRange(std::size_t(save), std::size_t(pos)));
                BlockNode b; b.id = id; b.kind = BlockKind::Frontmatter; b.ensure_atomic_special().fmt = fmt; b.ensure_atomic_special().raw = inner;
                return b;
            }
            if (!consume_line_ending()) advance();
        }
    }

    // ---- heading ----
    std::optional<BlockNode> parse_heading() {
        std::size_t start = pos;
        std::uint8_t level = 0;
        while (peek1() == '#' && level < 6) { advance(); ++level; }
        if (peek1() != U' ' && peek1() != U'\t') { pos = start; return std::nullopt; }
        while (peek1() == U' ' || peek1() == U'\t') advance();
        std::size_t content_start = pos;
        const auto line_end = line_content_end(pos);
        std::size_t trimmed_end = line_end;
        while (trimmed_end > content_start && (cps[trimmed_end - 1] == U' ' || cps[trimmed_end - 1] == U'\t')) --trimmed_end;
        std::size_t content_end = trimmed_end;
        std::size_t closing_start = trimmed_end;
        while (closing_start > content_start && cps[closing_start - 1] == U'#') --closing_start;
        if (closing_start < trimmed_end && closing_start > content_start && (cps[closing_start - 1] == U' ' || cps[closing_start - 1] == U'\t')) {
            content_end = closing_start - 1;
            while (content_end > content_start && (cps[content_end - 1] == U' ' || cps[content_end - 1] == U'\t')) --content_end;
        } else {
            closing_start = line_end;
            content_end = line_end;
        }
        pos = line_end;
        consume_line_ending();
        auto inline_content = make_inline_document(content_start, content_end);
        std::u32string title = trim_cps_(inline_visible_text(inline_content));
        std::string title_utf8 = cps_to_utf8(title);
        std::string slug = generate_slug(title_utf8, {});
        NodeId id = next_node_id();
        ParserSourceRange range(id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(content_start), std::size_t(content_end)));
        range.marker_ranges.push_back(PhysicalRange(std::size_t(start), std::size_t(content_start)));
        if (closing_start < line_end) range.marker_ranges.push_back(PhysicalRange(std::size_t(content_end), std::size_t(line_end)));
        source_ranges.push_back(std::move(range));
        BlockNode b; b.id = id; b.kind = BlockKind::Heading; b.ensure_text_special().level = level; b.inline_content = std::move(inline_content); b.ensure_text_special().slug = slug;
        b.ensure_text_special().opening_marker = std::u32string(std::u32string_view(cps).substr(start, content_start - start));
        if (closing_start < line_end) b.ensure_text_special().closing_marker = std::u32string(std::u32string_view(cps).substr(content_end, line_end - content_end));
        return b;
    }

    // The block parser owns paragraph boundaries only. Inline syntax is
    // parsed exactly once into the node-local lossless CST.
    std::optional<BlockNode> try_parse_link_definition() {
        const auto found = link_definitions_by_start.find(pos);
        if (found == link_definitions_by_start.end()) return std::nullopt;
        const auto start = pos;
        const auto& definition = found->second;
        pos = definition.source_end;
        consume_line_ending();
        BlockNode block;
        block.id = next_node_id();
        block.kind = BlockKind::LinkDefinition;
        block.ensure_atomic_special().raw = cps_to_utf8(std::u32string_view(cps).substr(start, definition.source_end - start));
        push_range(block.id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(start), std::size_t(definition.source_end)));
        return block;
    }

    std::optional<BlockNode> parse_paragraph(std::optional<std::size_t> stop_at = std::nullopt) {
        const auto start = pos;
        auto content_end = pos;
        const auto limit = (std::min)(cps.size(), stop_at.value_or(cps.size()));
        while (pos < limit) {
            if (!is_line_ending_character(peek1())) {
                advance();
                content_end = pos;
                continue;
            }
            const auto newline = pos;
            consume_line_ending();
            if (pos >= limit || is_line_ending_character(peek1()) || line_starts_interrupting_block()) {
                const auto hard_break = (newline >= start + 2 && cps[newline - 1] == U' ' && cps[newline - 2] == U' ')
                    || (newline > start && cps[newline - 1] == U'\\');
                content_end = hard_break ? pos : newline;
                break;
            }
            content_end = pos;
        }
        if (content_end == start) return std::nullopt;

        if (const auto underline = peek_line_start() ? setext_underline_at(pos) : std::nullopt) {
            pos = underline->line_end;
            consume_line_ending();
            auto inline_content = make_inline_document(start, content_end);
            auto title_utf8 = cps_to_utf8(inline_visible_text(inline_content));
            auto slug = generate_slug(title_utf8, {});
            const auto id = next_node_id();
            ParserSourceRange range(id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(start), std::size_t(content_end)));
            range.marker_ranges.push_back(PhysicalRange(std::size_t(content_end), std::size_t(underline->line_end)));
            source_ranges.push_back(std::move(range));
            BlockNode heading;
            heading.id = id;
            heading.kind = BlockKind::Heading;
            heading.ensure_text_special().level = underline->level;
            heading.inline_content = std::move(inline_content);
            heading.ensure_text_special().slug = std::move(slug);
            heading.ensure_text_special().closing_marker = std::u32string(std::u32string_view(cps).substr(content_end, underline->line_end - content_end));
            return heading;
        }

        const auto id = next_node_id();
        push_range(id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(start), std::size_t(content_end)));
        BlockNode paragraph;
        paragraph.id = id;
        paragraph.kind = BlockKind::Paragraph;
        paragraph.inline_content = make_inline_document(start, content_end);
        return paragraph;
    }

    std::optional<BlockNode> try_parse_image_block() {
        const auto start = pos;
        const auto line_end = line_content_end(pos);
        if (line_end == start) return std::nullopt;

        if (peek1() == U'<') {
            const auto tag = html_tag_at(start, line_end);
            if (tag && !tag->closing && tag->name == "img" && tag->end == line_end) {
                const auto source = tag->attributes.contains("src") ? tag->attributes.at("src") : std::string{};
                if (!safe_link_target(source, true)) return std::nullopt;
                auto dimension = [&](std::string_view name) -> std::optional<ImageDimension> {
                    const auto found = tag->attributes.find(std::string(name));
                    if (found == tag->attributes.end()) return std::nullopt;
                    return parse_html_image_dimension(found->second);
                };
                pos = line_end;
                consume_line_ending();
                BlockNode block;
                block.id = next_node_id();
                block.kind = BlockKind::ImageBlock;
                block.ensure_image_special().src = source;
                block.ensure_image_special().image_alt = tag->attributes.contains("alt") ? tag->attributes.at("alt") : std::string{};
                if (tag->attributes.contains("title")) block.ensure_image_special().image_title = tag->attributes.at("title");
                block.ensure_image_special().image_width = dimension("width");
                block.ensure_image_special().image_height = dimension("height");
                auto raw = std::u32string(std::u32string_view(cps).substr(
                    start,
                    line_end - start));
                auto& html = block.ensure_html_special();
                html.source = raw;
                html.tree = parse_html_cst(raw);
                html.structure_shape = html_block_structure_shape(block);
                html.root_tag = "img";
                push_range(block.id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(start), std::size_t(line_end)));
                return block;
            }
        }

        auto inline_document = make_inline_document(start, line_end);
        if (inline_document.tree.nodes.size() != 1) return std::nullopt;
        const auto& root = inline_document.tree.nodes.front();
        const InlineCstNode* image = nullptr;
        std::optional<std::string> link;
        if (root.kind == InlineCstKind::Image) {
            image = &root;
        } else if (root.kind == InlineCstKind::Link && root.children.size() == 1
            && root.children.front().kind == InlineCstKind::Image) {
            image = &root.children.front();
            link = root.semantics().href;
        }
        if (!image || image->status != ParseStatus::Complete) return std::nullopt;

        pos = line_end;
        consume_line_ending();
        BlockNode block;
        block.id = next_node_id();
        block.kind = BlockKind::ImageBlock;
        block.ensure_image_special().src = image->semantics().href;
        block.ensure_image_special().image_alt = image->semantics().alt;
        block.ensure_image_special().image_title = image->semantics().title;
        block.ensure_image_special().image_link = std::move(link);
        push_range(block.id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(start), std::size_t(line_end)));
        return block;
    }

    static bool html_whitespace_only(std::u32string_view value) {
        return std::ranges::all_of(value, [](char32_t cp) {
            return cp == U' ' || cp == U'\t' || cp == U'\r' || cp == U'\n';
        });
    }

    static bool html_contains_unsafe(const HtmlCstNode& node) {
        if (node.kind == HtmlCstKind::Element && html_is_unsafe_element(node.tag_name)) {
            return true;
        }
        return std::ranges::any_of(node.children, [](const auto& child) {
            return html_contains_unsafe(child);
        });
    }

    std::optional<std::u32string> html_attribute_value(
        const HtmlCstNode& node,
        std::string_view name,
        std::size_t source_start) const {
        const auto found = std::ranges::find_if(node.attributes, [&](const auto& attribute) {
            return attribute.name == name;
        });
        if (found == node.attributes.end() || !found->value_range) return std::nullopt;
        const auto range = *found->value_range;
        if (source_start + range.end > cps.size()) return std::nullopt;
        return std::u32string(std::u32string_view(cps).substr(
            source_start + range.start,
            range.length()));
    }

    static std::string_view trim_html_css_value(std::string_view value) {
        while (!value.empty() && static_cast<unsigned char>(value.front()) <= 0x20) {
            value.remove_prefix(1);
        }
        while (!value.empty() && static_cast<unsigned char>(value.back()) <= 0x20) {
            value.remove_suffix(1);
        }
        return value;
    }

    static std::string lower_html_css_value(std::string_view value) {
        std::string result(value);
        std::ranges::transform(result, result.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return result;
    }

    static std::optional<TextAlignment> parse_html_text_alignment(std::string_view value) {
        value = trim_html_css_value(value);
        auto lower = lower_html_css_value(value);
        if (const auto important = lower.find("!important"); important != std::string::npos) {
            lower = std::string(trim_html_css_value(std::string_view(lower).substr(0, important)));
        }
        if (lower == "left" || lower == "start") return TextAlignment::Start;
        if (lower == "center") return TextAlignment::Center;
        if (lower == "right" || lower == "end") return TextAlignment::End;
        if (lower == "justify") return TextAlignment::Justify;
        return std::nullopt;
    }

    std::optional<TextAlignment> html_text_alignment(
        const HtmlCstNode& node,
        std::size_t source_start) const {
        std::optional<TextAlignment> result;
        if (auto align = html_attribute_value(node, "align", source_start)) {
            result = parse_html_text_alignment(cps_to_utf8(*align));
        }
        auto style = html_attribute_value(node, "style", source_start);
        if (!style) return result;

        // CSS declarations cascade in source order. Only the safe enumerated
        // text-align property is projected; the exact style spelling remains
        // owned by the lossless HTML CST.
        const auto style_utf8 = cps_to_utf8(*style);
        auto declarations = std::string_view(style_utf8);
        while (!declarations.empty()) {
            const auto semicolon = declarations.find(';');
            auto declaration = trim_html_css_value(declarations.substr(0, semicolon));
            declarations = semicolon == std::string_view::npos
                ? std::string_view{}
                : declarations.substr(semicolon + 1);
            const auto colon = declaration.find(':');
            if (colon == std::string_view::npos) continue;
            auto property = lower_html_css_value(trim_html_css_value(declaration.substr(0, colon)));
            if (property != "text-align") continue;
            if (auto parsed = parse_html_text_alignment(declaration.substr(colon + 1))) {
                result = parsed;
            }
        }
        return result;
    }

    void push_html_range(
        NodeId id,
        const HtmlCstNode& node,
        std::size_t source_start) {
        ParserSourceRange range(
            id,
            {source_start + node.range.start, source_start + node.range.end},
            {source_start + node.content.start, source_start + node.content.end});
        if (!node.opening.empty()) {
            range.marker_ranges.push_back({
                source_start + node.opening.start,
                source_start + node.opening.end});
        }
        if (node.closing) {
            range.marker_ranges.push_back({
                source_start + node.closing->start,
                source_start + node.closing->end});
        }
        source_ranges.push_back(std::move(range));
    }

    void attach_html_element(
        BlockNode& block,
        const HtmlCstNode& node,
        std::size_t source_start) {
        auto& html = block.ensure_html_special();
        html.root_tag = node.tag_name;
        html.text_alignment = html_text_alignment(node, source_start);
        if (node.opening.valid_for(cps.size() - source_start)) {
            html.opening_marker = cps.substr(
                source_start + node.opening.start,
                node.opening.length());
        }
        if (node.closing && node.closing->valid_for(cps.size() - source_start)) {
            html.closing_marker = cps.substr(
                source_start + node.closing->start,
                node.closing->length());
        }
    }

    BlockNode make_html_inline_block(
        BlockKind kind,
        const HtmlCstNode& node,
        std::size_t source_start,
        std::vector<HtmlContentSlot>& slots,
        std::uint8_t heading_level = 0) {
        BlockNode block;
        block.id = next_node_id();
        block.kind = kind;
        block.inline_content = make_inline_document(
            source_start + node.content.start,
            source_start + node.content.end,
            InlineSyntaxMode::HtmlText);
        if (heading_level != 0) block.ensure_text_special().level = heading_level;
        attach_html_element(block, node, source_start);
        push_html_range(block.id, node, source_start);
        slots.push_back({block.id, node.content});
        return block;
    }

    BlockNode make_html_inline_run(
        SourceRange range,
        std::size_t source_start,
        std::vector<HtmlContentSlot>& slots) {
        BlockNode block;
        block.id = next_node_id();
        block.kind = BlockKind::Paragraph;
        block.inline_content = make_inline_document(
            source_start + range.start,
            source_start + range.end,
            InlineSyntaxMode::HtmlText);
        push_range(
            block.id,
            {source_start + range.start, source_start + range.end},
            {source_start + range.start, source_start + range.end});
        slots.push_back({block.id, range});
        return block;
    }

    void collect_html_rows(
        const HtmlCstNode& node,
        std::vector<const HtmlCstNode*>& rows,
        bool root = true) const {
        for (const auto& child : node.children) {
            if (child.kind != HtmlCstKind::Element) continue;
            if (child.tag_name == "tr") {
                rows.push_back(&child);
            } else if (child.tag_name != "table" || root) {
                collect_html_rows(child, rows, false);
            }
        }
    }

    std::optional<BlockNode> make_html_table(
        const HtmlCstNode& node,
        std::size_t source_start,
        std::vector<HtmlContentSlot>& slots) {
        std::vector<const HtmlCstNode*> html_rows;
        collect_html_rows(node, html_rows);
        if (html_rows.empty()) return std::nullopt;

        BlockNode table;
        table.id = next_node_id();
        table.kind = BlockKind::Table;
        std::size_t columns = 0;
        for (const auto* html_row : html_rows) {
            BlockNode row;
            row.id = next_node_id();
            row.kind = BlockKind::TableRow;
            bool header = false;
            for (const auto& html_cell : html_row->children) {
                if (html_cell.kind != HtmlCstKind::Element
                    || (html_cell.tag_name != "td" && html_cell.tag_name != "th")) {
                    continue;
                }
                header = header || html_cell.tag_name == "th";
                row.children.push_back(make_html_inline_block(
                    BlockKind::TableCell,
                    html_cell,
                    source_start,
                    slots));
            }
            if (row.children.empty()) continue;
            columns = (std::max)(columns, row.children.size());
            row.ensure_table_special().table_header_row = header;
            attach_html_element(row, *html_row, source_start);
            push_html_range(row.id, *html_row, source_start);
            table.children.push_back(std::move(row));
        }
        if (table.children.empty()) return std::nullopt;
        table.ensure_table_special().table_aligns.assign(columns, TableAlignment::None);
        attach_html_element(table, node, source_start);
        push_html_range(table.id, node, source_start);
        return table;
    }

    std::vector<BlockNode> make_html_children(
        const HtmlCstNode& container,
        std::size_t source_start,
        std::vector<HtmlContentSlot>& slots) {
        std::vector<BlockNode> blocks;
        std::optional<SourceRange> inline_run;
        auto flush_inline = [&]() {
            if (!inline_run) return;
            const auto source = std::u32string_view(cps).substr(
                source_start + inline_run->start,
                inline_run->length());
            if (!html_whitespace_only(source)) {
                blocks.push_back(make_html_inline_run(*inline_run, source_start, slots));
            }
            inline_run.reset();
        };

        for (const auto& child : container.children) {
            const auto is_block = child.kind == HtmlCstKind::Element
                && html_is_block_element(child.tag_name);
            if (!is_block) {
                if (!inline_run) inline_run = child.range;
                else inline_run->end = child.range.end;
                continue;
            }
            flush_inline();
            if (auto block = make_html_semantic(child, source_start, slots)) {
                blocks.push_back(std::move(*block));
            }
        }
        flush_inline();
        return blocks;
    }

    std::optional<BlockNode> make_html_list(
        const HtmlCstNode& node,
        std::size_t source_start,
        std::vector<HtmlContentSlot>& slots) {
        BlockNode list;
        list.id = next_node_id();
        list.kind = BlockKind::List;
        list.ensure_list_special().ordered = node.tag_name == "ol";
        if (auto start = html_attribute_value(node, "start", source_start)) {
            try {
                list.ensure_list_special().start = std::stoull(cps_to_utf8(*start));
            } catch (...) {
                list.ensure_list_special().start = 1;
            }
        }
        for (const auto& child : node.children) {
            if (child.kind != HtmlCstKind::Element || child.tag_name != "li") continue;
            BlockNode item;
            item.id = next_node_id();
            item.kind = BlockKind::ListItem;
            item.children = make_html_children(child, source_start, slots);
            if (item.children.empty()) {
                item.children.push_back(make_html_inline_run(
                    {child.content.start, child.content.start}, source_start, slots));
            }
            attach_html_element(item, child, source_start);
            push_html_range(item.id, child, source_start);
            list.children.push_back(std::move(item));
        }
        if (list.children.empty()) return std::nullopt;
        attach_html_element(list, node, source_start);
        push_html_range(list.id, node, source_start);
        return list;
    }

    std::optional<BlockNode> make_html_semantic(
        const HtmlCstNode& node,
        std::size_t source_start,
        std::vector<HtmlContentSlot>& slots) {
        if (node.kind != HtmlCstKind::Element) return std::nullopt;
        if (node.tag_name == "p" || node.tag_name == "summary"
            || node.tag_name == "dt" || node.tag_name == "dd") {
            return make_html_inline_block(BlockKind::Paragraph, node, source_start, slots);
        }
        if (node.tag_name.size() == 2 && node.tag_name[0] == 'h'
            && node.tag_name[1] >= '1' && node.tag_name[1] <= '6') {
            return make_html_inline_block(
                BlockKind::Heading,
                node,
                source_start,
                slots,
                static_cast<std::uint8_t>(node.tag_name[1] - '0'));
        }
        if (node.tag_name == "table") return make_html_table(node, source_start, slots);
        if (node.tag_name == "ul" || node.tag_name == "ol") {
            return make_html_list(node, source_start, slots);
        }
        if (node.tag_name == "blockquote") {
            BlockNode quote;
            quote.id = next_node_id();
            quote.kind = BlockKind::BlockQuote;
            quote.children = make_html_children(node, source_start, slots);
            if (quote.children.empty()) {
                quote.children.push_back(make_html_inline_run(
                    {node.content.start, node.content.start}, source_start, slots));
            }
            attach_html_element(quote, node, source_start);
            push_html_range(quote.id, node, source_start);
            return quote;
        }
        if (node.tag_name == "hr") {
            BlockNode rule;
            rule.id = next_node_id();
            rule.kind = BlockKind::ThematicBreak;
            attach_html_element(rule, node, source_start);
            push_html_range(rule.id, node, source_start);
            return rule;
        }
        if (node.tag_name == "pre") {
            BlockNode code;
            code.id = next_node_id();
            code.kind = BlockKind::CodeBlock;
            code.block_source = make_block_source(
                std::u32string(std::u32string_view(cps).substr(
                    source_start + node.range.start,
                    node.range.length())),
                BlockSourceKind::HtmlCode);
            attach_html_element(code, node, source_start);
            slots.push_back({code.id, node.range});
            push_html_range(code.id, node, source_start);
            return code;
        }

        BlockNode container;
        container.id = next_node_id();
        container.kind = BlockKind::HtmlContainer;
        container.children = make_html_children(node, source_start, slots);
        if (container.children.empty()) {
            container.children.push_back(make_html_inline_run(
                node.content, source_start, slots));
        }
        attach_html_element(container, node, source_start);
        push_html_range(container.id, node, source_start);
        return container;
    }

    std::optional<BlockNode> try_parse_raw_html_block() {
        const auto start = pos;
        auto remainder = std::u32string_view(cps).substr(start);
        auto scanned = parse_html_cst(remainder);
        if (scanned.nodes.empty()) return std::nullopt;
        const auto& first = scanned.nodes.front();
        if (first.kind != HtmlCstKind::Element
            || first.range.start != 0
            || !html_is_block_element(first.tag_name)
            || first.range.empty()) {
            return std::nullopt;
        }

        const auto source_length = first.range.end;
        auto raw = std::u32string(remainder.substr(0, source_length));
        auto tree = parse_html_cst(raw);
        if (tree.nodes.empty()) return std::nullopt;
        const auto& root = tree.nodes.front();

        if (tree.has_error || root.status != HtmlParseStatus::Complete
            || html_contains_unsafe(root)) {
            pos = start + source_length;
            consume_line_ending();
            BlockNode block;
            block.id = next_node_id();
            block.kind = BlockKind::UnsupportedMarkup;
            block.ensure_atomic_special().raw = cps_to_utf8(raw);
            block.ensure_atomic_special().unsup_reason = root.status == HtmlParseStatus::Complete
                ? UnsupportedMarkupReason::UnsafeHtml
                : UnsupportedMarkupReason::MalformedSyntax;
            push_range(
                block.id,
                {start, start + source_length},
                {start, start + source_length});
            diagnostics.push_back(make_diagnostic(
                DiagnosticSeverity::Warning,
                html_contains_unsafe(root)
                    ? "Unsafe HTML block is shown as text"
                    : "Malformed HTML block is shown as text",
                std::nullopt,
                DIAG_UNSAFE_HTML));
            return block;
        }

        std::vector<HtmlContentSlot> slots;
        auto block = make_html_semantic(root, start, slots);
        if (!block) return std::nullopt;
        // A <pre> block is itself one exact block-local source owner; all
        // other supported HTML structures retain their wrapper source plus
        // slots that point at editable semantic descendants.
        if (block->kind != BlockKind::CodeBlock) {
            auto root_tag = root.tag_name;
            auto& html = block->ensure_html_special();
            html.source = std::move(raw);
            html.tree = std::move(tree);
            html.content_slots = std::move(slots);
            html.structure_shape = html_block_structure_shape(*block);
            html.root_tag = std::move(root_tag);
        }
        pos = start + source_length;
        consume_line_ending();
        return block;
    }

    // ---- footnote definition ----
    std::optional<BlockNode> try_parse_footnote_definition() {
        const auto start = pos;
        const auto save = pos;
        advance_n(2);
        std::u32string label;
        while (!eof() && peek1() != ']' && !is_line_ending_character(peek1())) {
            label.push_back(peek1());
            advance();
        }
        if (peek1() != ']') { pos = save; return std::nullopt; }
        advance();
        if (peek1() != ':') { pos = save; return std::nullopt; }
        advance();
        if (peek1() == ' ') advance();
        const auto content_start = pos;

        struct BodyLine {
            std::size_t start = 0;
            std::size_t end = 0;
            std::size_t content_start = 0;
            bool blank = false;
        };
        auto inspect_line = [&](std::size_t line_start) {
            BodyLine line;
            line.start = line_start;
            line.end = line_content_end(line_start);
            line.blank = true;
            for (auto cursor = line.start; cursor < line.end; ++cursor) {
                if (cps[cursor] != U' ' && cps[cursor] != U'\t') {
                    line.blank = false;
                    break;
                }
            }
            line.content_start = line.start;
            if (!line.blank) {
                if (line.content_start < line.end && cps[line.content_start] == U'\t') {
                    ++line.content_start;
                } else {
                    std::size_t spaces = 0;
                    while (line.content_start < line.end && cps[line.content_start] == U' ' && spaces < 4) {
                        ++line.content_start;
                        ++spaces;
                    }
                    if (spaces < 4) line.content_start = line.start;
                }
            } else {
                line.content_start = line.end;
            }
            return line;
        };
        auto is_continuation = [](const BodyLine& line) {
            return !line.blank && line.content_start > line.start;
        };

        const auto first_line_end = line_content_end(content_start);
        std::vector<BodyLine> continuation_lines;
        auto source_end = next_line_start(first_line_end);
        auto cursor = source_end;
        while (cursor < cps.size()) {
            auto line = inspect_line(cursor);
            if (is_continuation(line)) {
                continuation_lines.push_back(line);
                source_end = next_line_start(line.end);
                cursor = source_end;
                continue;
            }
            if (!line.blank) break;

            std::vector<BodyLine> pending_blank_lines;
            auto lookahead = cursor;
            while (lookahead < cps.size()) {
                auto blank = inspect_line(lookahead);
                if (!blank.blank) break;
                pending_blank_lines.push_back(blank);
                lookahead = next_line_start(blank.end);
                if (lookahead == blank.end) break;
            }
            if (lookahead >= cps.size()) break;
            auto next = inspect_line(lookahead);
            if (!is_continuation(next)) break;
            continuation_lines.insert(
                continuation_lines.end(), pending_blank_lines.begin(), pending_blank_lines.end());
            continuation_lines.push_back(next);
            source_end = next_line_start(next.end);
            cursor = source_end;
        }

        std::u32string inner;
        std::vector<std::size_t> offset_map;
        std::vector<PhysicalRange> marker_ranges;
        marker_ranges.push_back(PhysicalRange(start, content_start));
        std::size_t inner_end_source = content_start;
        auto append_range = [&](std::size_t begin, std::size_t end) {
            for (auto source_offset = begin; source_offset < end; ++source_offset) {
                offset_map.push_back(source_offset);
                inner.push_back(cps[source_offset]);
            }
            inner_end_source = end;
        };
        append_range(content_start, first_line_end);
        for (const auto& line : continuation_lines) {
            offset_map.push_back(line.start > 0 ? line.start - 1 : line.start);
            inner.push_back(U'\n');
            if (!line.blank) {
                marker_ranges.push_back(PhysicalRange(line.start, line.content_start));
                append_range(line.content_start, line.end);
            } else {
                inner_end_source = line.end;
            }
        }
        offset_map.push_back(inner_end_source);

        ParseInput nested_input(input->revision, cps_to_utf8(inner), input->dialect);
        Parser nested(&nested_input);
        nested.node_counter = node_counter;
        auto children = nested.parse_blocks(nullptr);
        node_counter = nested.node_counter;
        auto remap = [&](std::size_t value) {
            return offset_map[(std::min)(value, offset_map.size() - 1)];
        };
        for (auto& range : nested.source_ranges) {
            range.source_range = PhysicalRange(remap(range.source_range.start), remap(range.source_range.end));
            range.content_range = PhysicalRange(remap(range.content_range.start), remap(range.content_range.end));
            for (auto& prefix : range.marker_ranges) {
                prefix = PhysicalRange(remap(prefix.start), remap(prefix.end));
            }
            source_ranges.push_back(std::move(range));
        }
        if (children.empty()) {
            BlockNode paragraph;
            paragraph.id = next_node_id();
            paragraph.kind = BlockKind::Paragraph;
            source_ranges.emplace_back(
                paragraph.id,
                PhysicalRange(content_start, content_start),
                PhysicalRange(content_start, content_start));
            children.push_back(std::move(paragraph));
        }
        for (auto& diagnostic : nested.diagnostics) diagnostics.push_back(std::move(diagnostic));
        pos = source_end;
        const auto id = next_node_id();
        ParserSourceRange definition_range(
            id,
            PhysicalRange(start, source_end),
            PhysicalRange(content_start, inner_end_source));
        definition_range.marker_ranges = std::move(marker_ranges);
        source_ranges.push_back(std::move(definition_range));
        BlockNode block;
        block.id = id;
        block.kind = BlockKind::FootnoteDefinition;
        block.ensure_container_special().footnote_label = cps_to_utf8(label);
        block.ensure_text_special().opening_marker = std::u32string(std::u32string_view(cps).substr(start, content_start - start));
        block.children = std::move(children);
        return block;
    }

    // ---- code fence / fenced math ----
    std::optional<BlockNode> try_parse_indented_code_block() {
        auto indentation_end = [&](std::size_t line_start) -> std::optional<std::size_t> {
            if (line_start >= cps.size()) return std::nullopt;
            if (cps[line_start] == U'\t') return line_start + 1;
            auto cursor = line_start;
            while (cursor < cps.size() && cursor - line_start < 4 && cps[cursor] == U' ') ++cursor;
            if (cursor - line_start == 4) return cursor;
            return std::nullopt;
        };
        auto start = pos;
        if (!indentation_end(start)) return std::nullopt;
        std::u32string text;
        std::vector<PhysicalRange> markers;
        std::size_t content_start = start;
        std::size_t content_end = start;
        bool first_line = true;
        while (pos < cps.size()) {
            auto line_start = pos;
            const auto line_end = line_content_end(line_start);
            auto indent_end = indentation_end(line_start);
            if (!indent_end) {
                bool blank = true;
                for (auto cursor = line_start; cursor < line_end; ++cursor) {
                    if (cps[cursor] != U' ' && cps[cursor] != U'\t') { blank = false; break; }
                }
                if (!blank) break;
                auto next = line_end + line_ending_length(line_end);
                while (next < cps.size()) {
                    const auto next_end = line_content_end(next);
                    bool next_blank = true;
                    for (auto cursor = next; cursor < next_end; ++cursor) {
                        if (cps[cursor] != U' ' && cps[cursor] != U'\t') { next_blank = false; break; }
                    }
                    if (!next_blank) break;
                    next = next_end + line_ending_length(next_end);
                }
                if (next >= cps.size() || !indentation_end(next)) break;
                markers.push_back(PhysicalRange(std::size_t(line_start), std::size_t(line_end)));
                text.push_back(U'\n');
                pos = line_end;
                consume_line_ending();
                content_end = line_end;
                continue;
            }
            markers.push_back(PhysicalRange(std::size_t(line_start), std::size_t(*indent_end)));
            if (first_line) content_start = *indent_end;
            first_line = false;
            text.append(cps.begin() + *indent_end, cps.begin() + line_end);
            content_end = line_end;
            if (line_end < cps.size()) text.push_back(U'\n');
            pos = line_end;
            consume_line_ending();
        }
        NodeId id = next_node_id();
        ParserSourceRange range(id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(content_start), std::size_t(content_end)));
        range.marker_ranges = std::move(markers);
        source_ranges.push_back(std::move(range));
        BlockNode block;
        block.id = id;
        block.kind = BlockKind::CodeBlock;
        block.ensure_atomic_special().code_indented = true;
        block.block_source = make_block_source(
            std::u32string(std::u32string_view(cps).substr(start, pos - start)),
            BlockSourceKind::IndentedCode);
        return block;
    }

    std::optional<BlockNode> try_parse_code_block() {
        std::size_t start = pos;
        std::size_t opening_indent = 0;
        while (opening_indent < 4 && peek1() == U' ') {
            ++opening_indent;
            advance();
        }
        if (opening_indent > 3) { pos = start; return std::nullopt; }

        const auto marker = peek1();
        if (marker != U'`' && marker != U'~') { pos = start; return std::nullopt; }
        std::size_t count = 0;
        while (peek1() == marker) { ++count; advance(); }
        if (count < 3) { pos = start; return std::nullopt; }
        auto [info_line, info_length] = rest_of_line();
        advance_n(info_length);
        consume_line_ending();
        std::size_t content_start = pos;
        std::size_t content_end = pos;
        auto info_utf8 = cps_to_utf8(info_line);
        std::optional<std::string> lang;
        auto trimmed = trim_utf8(info_utf8);
        if (!trimmed.empty()) lang = trimmed;
        std::u32string text;
        std::optional<PhysicalRange> closing_marker;
        while (!eof()) {
            std::size_t line_start = pos;
            std::size_t scan = pos;
            std::size_t closing_indent = 0;
            while (scan < cps.size() && cps[scan] == U' ' && closing_indent < 4) {
                ++scan;
                ++closing_indent;
            }
            const auto fence_start = scan;
            while (scan < cps.size() && cps[scan] == marker) ++scan;
            std::size_t fence_count = scan - fence_start;
            std::size_t marker_end = scan;
            while (marker_end < cps.size() && (cps[marker_end] == U' ' || cps[marker_end] == U'\t')) ++marker_end;
            if (closing_indent <= 3
                && fence_count >= count
                && (marker_end == cps.size() || is_line_ending_character(cps[marker_end]))) {
                content_end = line_start;
                closing_marker = PhysicalRange(std::size_t(line_start), std::size_t(marker_end));
                pos = marker_end;
                consume_line_ending();
                break;
            }
            while (!eof() && !is_line_ending_character(peek1())) {
                text.push_back(peek1());
                advance();
            }
            if (is_line_ending_character(peek1())) {
                text.push_back(U'\n');
                consume_line_ending();
            }
            content_end = pos;
        }
        if (content_end < content_start) content_end = pos;
        NodeId id = next_node_id();
        ParserSourceRange node_range(id, PhysicalRange(std::size_t(start), cur()), PhysicalRange(std::size_t(content_start), std::size_t(content_end)));
        node_range.marker_ranges.push_back(PhysicalRange(std::size_t(start), std::size_t(content_start)));
        if (closing_marker) node_range.marker_ranges.push_back(*closing_marker);
        source_ranges.push_back(std::move(node_range));
        if (lang && *lang == "math" && input->dialect.math.fenced_math) {
            const auto local_end = closing_marker ? closing_marker->end : pos;
            BlockNode b; b.id = id; b.kind = BlockKind::MathBlock; b.ensure_atomic_special().math_delim = MathDelimiter::FencedMath;
            b.block_source = make_block_source(
                std::u32string(std::u32string_view(cps).substr(start, local_end - start)),
                BlockSourceKind::FencedMath);
            return b;
        }
        const auto local_end = closing_marker ? closing_marker->end : pos;
        BlockNode b; b.id = id; b.kind = BlockKind::CodeBlock;
        b.block_source = make_block_source(
            std::u32string(std::u32string_view(cps).substr(start, local_end - start)),
            BlockSourceKind::FencedCode);
        return b;
    }

    // ---- math blocks `$$ ... $$` and `\\[ ... \\]` ----
    std::optional<BlockNode> try_parse_math_block() {
        std::size_t start = pos;
        bool dollar = peek1() == '$' && peek2() == '$' && input->dialect.math.block_dollar;
        bool bracket = peek1() == '\\' && peek2() == '[' && input->dialect.math.block_bracket;
        if (!dollar && !bracket) return std::nullopt;
        advance_n(2);
        consume_line_ending();
        const auto content_start = pos;
        std::u32string tex;
        while (!eof()) {
            bool closed = dollar ? (peek1() == '$' && peek2() == '$') : (peek1() == '\\' && peek2() == ']');
            if (closed) {
                const auto closing_start = pos;
                advance_n(2);
                const auto closing_end = pos;
                consume_line_ending();
                NodeId id = next_node_id();
                ParserSourceRange range(
                    id,
                    PhysicalRange(std::size_t(start), cur()),
                    PhysicalRange(std::size_t(content_start), std::size_t(closing_start)));
                range.marker_ranges.push_back(PhysicalRange(std::size_t(start), std::size_t(content_start)));
                range.marker_ranges.push_back(PhysicalRange(std::size_t(closing_start), std::size_t(closing_end)));
                source_ranges.push_back(std::move(range));
                BlockNode b; b.id = id; b.kind = BlockKind::MathBlock; b.ensure_atomic_special().math_delim = dollar ? MathDelimiter::BlockDollar : MathDelimiter::BlockBracket;
                b.block_source = make_block_source(
                    std::u32string(std::u32string_view(cps).substr(start, closing_end - start)),
                    dollar ? BlockSourceKind::DollarMath : BlockSourceKind::BracketMath);
                return b;
            }
            tex.push_back(peek1()); advance();
        }
        diagnostics.push_back(make_diagnostic(DiagnosticSeverity::Warning,
            dollar ? "Unclosed math block delimiter $$" : "Unclosed math block delimiter \\[",
           std::nullopt, DIAG_UNCLOSED_MATH_DOLLAR));
        NodeId id = next_node_id();
        ParserSourceRange range(
            id,
            PhysicalRange(std::size_t(start), cur()),
            PhysicalRange(std::size_t(content_start), cur()));
        range.marker_ranges.push_back(PhysicalRange(std::size_t(start), std::size_t(content_start)));
        source_ranges.push_back(std::move(range));
        BlockNode b; b.id = id; b.kind = BlockKind::MathBlock; b.ensure_atomic_special().math_delim = dollar ? MathDelimiter::BlockDollar : MathDelimiter::BlockBracket;
        b.block_source = make_block_source(
            std::u32string(std::u32string_view(cps).substr(start, pos - start)),
            dollar ? BlockSourceKind::DollarMath : BlockSourceKind::BracketMath);
        return b;
    }

    // ---- table ----
    std::optional<BlockNode> try_parse_table() {
        std::size_t save = pos;
        std::size_t range_count = source_ranges.size();
        std::uint64_t node_count = node_counter;
        auto fail = [&]() -> std::optional<BlockNode> {
            pos = save;
            source_ranges.resize(range_count);
            node_counter = node_count;
            return std::nullopt;
        };
        auto [header_line, _] = rest_of_line();
        if (header_line.find(U'|') == std::u32string::npos) return fail();
        const auto header_end = save + header_line.size();
        std::vector<std::u32string> physical_line_endings;
        physical_line_endings.emplace_back(std::u32string_view(cps).substr(
            header_end,
            next_line_start(header_end) - header_end));
        auto header_row = parse_table_row();
        if (!header_row) return fail();
        const auto separator_start = pos;
        auto [separator_line, separator_length] = rest_of_line();
        auto alignments = parse_table_separator(separator_line);
        if (!alignments || alignments->size() != header_row->children.size()) return fail();
        advance_n(separator_length);
        const auto separator_end = separator_start + separator_length;
        physical_line_endings.emplace_back(std::u32string_view(cps).substr(
            separator_end,
            next_line_start(separator_end) - separator_end));
        consume_line_ending();
        BlockVec rows;
        while (!eof()) {
            std::size_t row_start = pos;
            auto [line, row_length] = rest_of_line();
            if (line.find(U'|') == std::u32string::npos) break;
            const auto row_end = row_start + row_length;
            auto row = parse_table_row(header_row->children.size());
            if (!row) break;
            physical_line_endings.emplace_back(std::u32string_view(cps).substr(
                row_end,
                next_line_start(row_end) - row_end));
            while (row->children.size() < header_row->children.size()) {
                BlockNode cell;
                cell.id = next_node_id();
                cell.kind = BlockKind::TableCell;
                cell.inline_content = make_inline_document(row_start + row_length, row_start + row_length);
                auto offset = std::size_t(row_start + row_length);
                push_range(cell.id, PhysicalRange(offset, offset), PhysicalRange(offset, offset));
                row->children.push_back(std::move(cell));
            }
            rows.push_back(std::move(*row));
        }
        NodeId id = next_node_id();
        push_range(id, PhysicalRange(std::size_t(save), cur()), PhysicalRange(std::size_t(save), cur()));
        BlockNode b; b.id = id; b.kind = BlockKind::Table;
        header_row->ensure_table_special().table_header_row = true;
        b.children.push_back(std::move(*header_row));
        b.children.insert(b.children.end(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
        auto& table = b.ensure_table_special();
        table.table_aligns = std::move(*alignments);
        table.table_separator_source = std::move(separator_line);
        physical_line_endings.resize(b.children.size());
        table.table_internal_line_endings = std::move(physical_line_endings);
        return b;
    }

    std::vector<std::pair<std::size_t, std::size_t>> table_cell_segments(const std::u32string& line) const {
        std::vector<std::pair<std::size_t, std::size_t>> segments;
        std::size_t start = line.empty() || line.front() != U'|' ? 0 : 1;
        for (std::size_t index = start; index < line.size(); ++index) {
            if (line[index] == U'\\' && index + 1 < line.size()) {
                ++index;
                continue;
            }
            if (line[index] != U'|') continue;
            segments.push_back({start, index});
            start = index + 1;
        }
        if (start < line.size()) segments.push_back({start, line.size()});
        return segments;
    }

    std::optional<BlockNode> parse_table_row(std::size_t maximum_cells = (std::numeric_limits<std::size_t>::max)()) {
        std::size_t row_start = pos;
        auto [line, length] = rest_of_line();
        auto segments = table_cell_segments(line);
        if (segments.empty()) return std::nullopt;
        BlockVec cells;
        cells.reserve(segments.size());
        for (auto const& segment : segments) {
            if (cells.size() >= maximum_cells) break;
            std::size_t content_start = segment.first;
            std::size_t content_end = segment.second;
            while (content_start < content_end && (line[content_start] == U' ' || line[content_start] == U'\t')) ++content_start;
            while (content_end > content_start && (line[content_end - 1] == U' ' || line[content_end - 1] == U'\t')) --content_end;
            if (content_start == segment.second) {
                content_start = segment.first;
                if (content_start < segment.second && (line[content_start] == U' ' || line[content_start] == U'\t')) ++content_start;
                content_end = content_start;
            }
            NodeId cell_id = next_node_id();
            auto source_start = std::size_t(row_start + segment.first);
            auto source_end = std::size_t(row_start + segment.second);
            auto text_start = std::size_t(row_start + content_start);
            auto text_end = std::size_t(row_start + content_end);
            BlockNode cell;
            cell.id = cell_id;
            cell.kind = BlockKind::TableCell;
            cell.inline_content = make_inline_document(text_start, text_end);
            push_range(cell_id, PhysicalRange(source_start, source_end), PhysicalRange(text_start, text_end));
            cells.push_back(std::move(cell));
        }
        advance_n(length);
        consume_line_ending();
        NodeId row_id = next_node_id();
        push_range(row_id, PhysicalRange(std::size_t(row_start), cur()), PhysicalRange(std::size_t(row_start), std::size_t(row_start + length)));
        BlockNode r; r.id = row_id; r.kind = BlockKind::TableRow; r.children = std::move(cells);
        return r;
    }

    std::optional<std::vector<TableAlignment>> parse_table_separator(const std::u32string& line) const {
        auto segments = table_cell_segments(line);
        if (segments.empty()) return std::nullopt;
        std::vector<TableAlignment> alignments;
        for (auto const& segment : segments) {
            auto marker = trim_cps_(line.substr(segment.first, segment.second - segment.first));
            if (marker.empty()) return std::nullopt;
            bool leading_colon = marker.front() == U':';
            bool trailing_colon = marker.back() == U':';
            std::size_t begin = leading_colon ? 1 : 0;
            std::size_t end = marker.size() - (trailing_colon ? 1 : 0);
            if (begin >= end) return std::nullopt;
            for (std::size_t index = begin; index < end; ++index) if (marker[index] != U'-') return std::nullopt;
            TableAlignment alignment = TableAlignment::None;
            if (leading_colon && trailing_colon) alignment = TableAlignment::Center;
            else if (leading_colon) alignment = TableAlignment::Left;
            else if (trailing_colon) alignment = TableAlignment::Right;
            alignments.push_back(alignment);
        }
        return alignments;
    }

    // ---- list / task ----
    std::optional<BlockNode> try_parse_list_or_task() {
        struct Marker {
            bool ordered = false;
            bool task = false;
            bool checked = false;
            std::uint64_t number = 1;
            char32_t delimiter = U'.';
            std::size_t start = 0;
            std::size_t indent = 0;
            std::size_t content_start = 0;
            std::size_t content_end = 0;
            std::size_t source_end = 0;
            std::u32string text;
        };
        auto inspect = [&](std::size_t at) -> std::optional<Marker> {
            if (at >= cps.size() || !is_line_start_at(at)) return std::nullopt;
            Marker marker;
            marker.start = at;
            std::size_t cursor = at;
            while (cursor < cps.size() && cps[cursor] == U' ' && marker.indent < 4) { ++cursor; ++marker.indent; }
            if (marker.indent > 3 || cursor >= cps.size()) return std::nullopt;
            if (cps[cursor] == U'-' || cps[cursor] == U'*' || cps[cursor] == U'+') {
                ++cursor;
            } else if (is_ascii_digit_(cps[cursor])) {
                marker.ordered = true;
                marker.number = 0;
                while (cursor < cps.size() && is_ascii_digit_(cps[cursor])) {
                    marker.number = marker.number * 10 + static_cast<std::uint64_t>(cps[cursor] - U'0');
                    ++cursor;
                }
                if (cursor >= cps.size() || (cps[cursor] != U'.' && cps[cursor] != U')')) return std::nullopt;
                marker.delimiter = cps[cursor++];
            } else {
                return std::nullopt;
            }
            if (cursor >= cps.size() || (cps[cursor] != U' ' && cps[cursor] != U'\t')) return std::nullopt;
            while (cursor < cps.size() && (cps[cursor] == U' ' || cps[cursor] == U'\t')) ++cursor;
            if (!marker.ordered && cursor + 2 < cps.size() && cps[cursor] == U'[' && (cps[cursor + 1] == U' ' || cps[cursor + 1] == U'x' || cps[cursor + 1] == U'X') && cps[cursor + 2] == U']') {
                marker.task = true;
                marker.checked = cps[cursor + 1] == U'x' || cps[cursor + 1] == U'X';
                cursor += 3;
                if (cursor < cps.size() && cps[cursor] == U' ') ++cursor;
            }
            marker.content_start = cursor;
            cursor = line_content_end(cursor);
            marker.content_end = cursor;
            marker.source_end = next_line_start(cursor);
            marker.text = std::u32string(cps.begin() + marker.start, cps.begin() + marker.content_start);
            return marker;
        };

        auto first = inspect(pos);
        if (!first) return std::nullopt;
        auto start = pos;
        auto first_content = first->content_start;
        auto last_content = first->content_end;
        NodeId list_id = next_node_id();
        BlockNode result;
        result.id = list_id;
        result.kind = first->task ? BlockKind::TaskList : BlockKind::List;
        result.ensure_list_special().ordered = first->ordered;
        result.ensure_list_special().start = first->number;
        result.ensure_list_special().delimiter = first->delimiter;

        while (auto marker = inspect(pos)) {
            if (marker->indent != first->indent || marker->task != first->task || marker->ordered != first->ordered) break;
            NodeId item_id = next_node_id();
            auto item_end = marker->source_end;
            bool previous_blank = false;
            std::optional<std::size_t> trailing_blank_start;
            while (item_end < cps.size()) {
                auto line_start = item_end;
                const auto line_end = line_content_end(line_start);
                bool blank = true;
                std::size_t indentation = 0;
                for (auto cursor = line_start; cursor < line_end; ++cursor) {
                    if (cps[cursor] == U' ' && blank) ++indentation;
                    else if (cps[cursor] != U' ' && cps[cursor] != U'\t') blank = false;
                }
                auto next_marker = inspect(line_start);
                if (next_marker && next_marker->indent == first->indent) break;
                if (!blank && indentation <= first->indent && previous_blank) {
                    // A blank run before an outdented sibling is the separator
                    // between blocks, not trailing source owned by this list
                    // item. Leave it for parse_blocks so separator_before can
                    // preserve the exact number of blank lines.
                    item_end = trailing_blank_start.value_or(item_end);
                    break;
                }
                if (!blank && indentation <= first->indent
                    && line_starts_interrupting_block(line_start + indentation)) {
                    // Lazy paragraph continuations may remain unindented, but
                    // an explicit peer-level block marker ends the item even
                    // without a separating blank line. Otherwise the nested
                    // parse would incorrectly absorb the following heading,
                    // quote, fence, list, math or HTML block into this item.
                    break;
                }
                previous_blank = blank;
                if (blank) {
                    if (!trailing_blank_start) trailing_blank_start = line_start;
                } else {
                    trailing_blank_start.reset();
                }
                item_end = next_line_start(line_end);
            }

            std::u32string inner;
            std::vector<std::size_t> offset_map;
            std::size_t inner_end_source = marker->content_start;
            auto append_range = [&](std::size_t begin, std::size_t end) {
                for (auto cursor = begin; cursor < end; ++cursor) {
                    offset_map.push_back(cursor);
                    inner.push_back(cps[cursor]);
                }
                inner_end_source = end;
            };
            append_range(marker->content_start, marker->content_end);
            auto line_start = marker->source_end;
            auto content_indent = marker->content_start - marker->start;
            while (line_start < item_end) {
                const auto line_end = (std::min)(line_content_end(line_start), item_end);
                offset_map.push_back(line_start > 0 ? line_start - 1 : line_start);
                inner.push_back(U'\n');
                inner_end_source = line_start;
                auto content = line_start;
                std::size_t removed = 0;
                while (content < line_end && removed < content_indent && (cps[content] == U' ' || cps[content] == U'\t')) { ++content; ++removed; }
                append_range(content, line_end);
                line_start = line_end < item_end ? next_line_start(line_end) : line_end;
            }
            offset_map.push_back(inner_end_source);
            ParseInput nested_input(input->revision, cps_to_utf8(inner), input->dialect);
            Parser nested(&nested_input);
            nested.node_counter = node_counter;
            auto children = nested.parse_blocks(nullptr);
            node_counter = nested.node_counter;
            auto remap = [&](std::size_t value) {
                return std::size_t(offset_map[(std::min)(value, offset_map.size() - 1)]);
            };
            for (auto& range : nested.source_ranges) {
                range.source_range = PhysicalRange(remap(range.source_range.start), remap(range.source_range.end));
                range.content_range = PhysicalRange(remap(range.content_range.start), remap(range.content_range.end));
                for (auto& prefix : range.marker_ranges) prefix = PhysicalRange(remap(prefix.start), remap(prefix.end));
                source_ranges.push_back(std::move(range));
            }
            if (children.empty()) {
                BlockNode paragraph;
                paragraph.id = next_node_id();
                paragraph.kind = BlockKind::Paragraph;
                source_ranges.emplace_back(
                    paragraph.id,
                    PhysicalRange(std::size_t(marker->content_start), std::size_t(marker->content_start)),
                    PhysicalRange(std::size_t(marker->content_start), std::size_t(marker->content_start)));
                children.push_back(std::move(paragraph));
            }
            for (auto& diagnostic : nested.diagnostics) {
                diagnostics.push_back(std::move(diagnostic));
            }
            ParserSourceRange item_range(item_id, PhysicalRange(std::size_t(marker->start), std::size_t(item_end)), PhysicalRange(std::size_t(marker->content_start), std::size_t(inner_end_source)));
            item_range.marker_ranges.push_back(PhysicalRange(std::size_t(marker->start), std::size_t(marker->content_start)));
            source_ranges.push_back(std::move(item_range));
            BlockNode item;
            item.id = item_id;
            item.kind = marker->task ? BlockKind::TaskListItem : BlockKind::ListItem;
            item.ensure_item_special().checked = marker->checked;
            item.ensure_item_special().marker = marker->text;
            item.children = std::move(children);
            result.children.push_back(std::move(item));
            last_content = item_end;
            pos = item_end;
            if (pos >= cps.size()) break;
        }
        push_range(list_id, PhysicalRange(std::size_t(start), std::size_t(pos)), PhysicalRange(std::size_t(first_content), std::size_t(last_content)));
        return result;
    }

    // ---- blockquote / callout ----
    std::optional<BlockNode> parse_blockquote_or_callout() {
        std::size_t start = pos;
        std::u32string inner;
        std::vector<std::size_t> offset_map;
        std::vector<PhysicalRange> marker_ranges;
        std::size_t content_start = start;
        std::size_t content_end = start;
        bool first = true;
        bool last_line_empty = false;
        while (pos < cps.size() && peek_line_start() && peek1() == U'>') {
            auto marker_start = pos;
            advance();
            if (peek1() == U' ') advance();
            auto line_content_start = pos;
            last_line_empty = is_line_ending_character(peek1()) || eof();
            if (first) content_start = line_content_start;
            first = false;
            marker_ranges.push_back(PhysicalRange(std::size_t(marker_start), std::size_t(line_content_start)));
            while (!eof() && !is_line_ending_character(peek1())) {
                offset_map.push_back(pos);
                inner.push_back(peek1());
                advance();
            }
            content_end = pos;
            if (is_line_ending_character(peek1())) {
                offset_map.push_back(pos);
                inner.push_back(U'\n');
                consume_line_ending();
            }
            if (!(pos < cps.size() && peek_line_start() && peek1() == U'>')) break;
        }
        auto source_end = pos;
        offset_map.push_back(source_end);
        auto first_newline = inner.find(U'\n');
        auto first_line = first_newline == std::u32string::npos ? inner : inner.substr(0, first_newline);
        auto callout = try_parse_callout_from_line(first_line);
        if (callout) {
            callout->ensure_text_special().opening_marker = std::u32string(
                std::u32string_view(cps).substr(start, content_start - start))
                + callout->text_special().opening_marker;
            if (const auto* title = callout_title_block(*callout)) {
                const auto title_start = first_line.size() - title->inline_content.source.size();
                source_ranges.emplace_back(
                    title->id,
                    PhysicalRange(offset_map[title_start], offset_map[first_line.size()]),
                    PhysicalRange(offset_map[title_start], offset_map[first_line.size()]));
            }
        }
        auto body_start = callout ? (first_newline == std::u32string::npos ? inner.size() : first_newline + 1) : 0;
        auto body = inner.substr(body_start);
        ParseInput nested_input(input->revision, cps_to_utf8(body), input->dialect);
        Parser nested(&nested_input);
        nested.node_counter = node_counter;
        auto children = nested.parse_blocks(nullptr);
        node_counter = nested.node_counter;
        auto remap = [&](std::size_t value) {
            auto index = (std::min)(body_start + value, offset_map.size() - 1);
            return std::size_t(offset_map[index]);
        };
        for (auto& range : nested.source_ranges) {
            range.source_range = PhysicalRange(remap(range.source_range.start), remap(range.source_range.end));
            range.content_range = PhysicalRange(remap(range.content_range.start), remap(range.content_range.end));
            for (auto& marker : range.marker_ranges) marker = PhysicalRange(remap(marker.start), remap(marker.end));
            source_ranges.push_back(std::move(range));
        }
        if (children.empty()) {
            BlockNode paragraph;
            paragraph.id = next_node_id();
            paragraph.kind = BlockKind::Paragraph;
            source_ranges.emplace_back(
                paragraph.id,
                PhysicalRange(std::size_t(content_start), std::size_t(content_start)),
                PhysicalRange(std::size_t(content_start), std::size_t(content_start)));
            children.push_back(std::move(paragraph));
        } else if (last_line_empty) {
            BlockNode paragraph;
            paragraph.id = next_node_id();
            paragraph.kind = BlockKind::Paragraph;
            children.push_back(std::move(paragraph));
        }
        for (auto& diagnostic : nested.diagnostics) {
            diagnostics.push_back(std::move(diagnostic));
        }
        NodeId id = next_node_id();
        ParserSourceRange quote_range(id, PhysicalRange(std::size_t(start), std::size_t(source_end)), PhysicalRange(std::size_t(content_start), std::size_t(content_end)));
        quote_range.marker_ranges = std::move(marker_ranges);
        source_ranges.push_back(std::move(quote_range));
        if (callout) {
            callout->id = id;
            callout->children.insert(
                callout->children.end(),
                std::make_move_iterator(children.begin()),
                std::make_move_iterator(children.end()));
            return callout;
        }
        BlockNode b; b.id = id; b.kind = BlockKind::BlockQuote; b.children = std::move(children);
        return b;
    }
    std::optional<BlockNode> try_parse_callout_from_line(const std::u32string& line) {
        std::size_t marker_start = 0;
        while (marker_start < line.size()
            && (line[marker_start] == U' ' || line[marker_start] == U'\t')) ++marker_start;
        if (marker_start + 1 < line.size()
            && line[marker_start] == U'[' && line[marker_start + 1] == U'!') {
            std::size_t kind_end = std::u32string::npos;
            for (std::size_t i = marker_start + 2; i < line.size(); ++i) {
                if (line[i] == U']') { kind_end = i; break; }
            }
            if (kind_end == std::u32string::npos) return std::nullopt;
            std::u32string kind = substr_cps_(line, marker_start + 2, kind_end - marker_start - 2);
            auto normalized_kind = normalize_callout_kind(cps_to_utf8(kind));
            if (!normalized_kind) return std::nullopt;
            auto title_start = kind_end + 1;
            if (title_start < line.size()
                && (line[title_start] == U' ' || line[title_start] == U'\t')) ++title_start;
            std::u32string title_part = title_start < line.size()
                ? substr_cps_(line, title_start, std::u32string::npos)
                : U"";
            if (!title_part.empty()) {
                BlockNode title;
                title.id = next_node_id();
                title.kind = BlockKind::CalloutTitle;
                title.inline_content.source = std::move(title_part);
                title.inline_content.tree = parse_inline(
                    title.inline_content.source,
                    inline_parse_context());
                BlockNode b;
                b.id = NodeId(0);
                b.kind = BlockKind::Callout;
                b.ensure_container_special().callout_kind = *normalized_kind;
                b.children.push_back(std::move(title));
                b.ensure_text_special().opening_marker = line.substr(0, title_start);
                return b;
            }
            BlockNode b; b.id = NodeId(0); b.kind = BlockKind::Callout; b.ensure_container_special().callout_kind = *normalized_kind;
            b.ensure_text_special().opening_marker = line.substr(0, title_start);
            return b;
        }
        return std::nullopt;
    }

    // ---- thematic break ----
    std::optional<BlockNode> try_parse_thematic_break() {
        std::size_t start = pos;
        std::size_t line_end = start;
        if (!line_is_thematic_break(start, &line_end)) return std::nullopt;
        pos = line_end;
        consume_line_ending();
        NodeId id = next_node_id();
        push_range(id, PhysicalRange(std::size_t(start), std::size_t(line_end)), PhysicalRange(std::size_t(start), std::size_t(line_end)));
        BlockNode b; b.id = id; b.kind = BlockKind::ThematicBreak;
        return b;
    }

    // ---- TOC ----
    bool try_match_toc() {
        if (peek1()=='[' && peek(1)=='T' && peek(2)=='O' && peek(3)=='C' && peek(4)==']') return true;
        if (peek1()=='[' && peek(1)=='[' && peek(2)=='t' && peek(3)=='o' && peek(4)=='c' && peek(5)==']' && peek(6)==']') return true;
        return false;
    }
    std::optional<BlockNode> parse_toc() {
        std::size_t start = pos;
        TocMarkerKind mk = TocMarkerKind::BracketToc;
        if (peek1()=='[' && peek2()=='T') { advance_n(5); mk = TocMarkerKind::BracketToc; }
        else { advance_n(7); mk = TocMarkerKind::WikiToc; }
        consume_line_ending();
        NodeId id = next_node_id();
        push_range(id, PhysicalRange(std::size_t(start), cur()), PhysicalRange(std::size_t(start), cur()));
        BlockNode b; b.id = id; b.kind = BlockKind::Toc; b.ensure_atomic_special().toc_marker = mk;
        return b;
    }

    // helpers
    static bool is_alnum_(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
    static bool is_ascii_digit_(char32_t c) { return c >= '0' && c <= '9'; }
    static std::u32string to_lower_(const std::u32string& s) {
        std::u32string r = s;
        for (auto& c : r) if (c >= 'A' && c <= 'Z') c = static_cast<char32_t>(c - 'A' + 'a');
        return r;
    }
    static bool starts_with_(const std::u32string& s, std::u32string_view p) {
        if (s.size() < p.size()) return false;
        for (std::size_t i = 0; i < p.size(); ++i) if (s[i] != p[i]) return false;
        return true;
    }
    static std::u32string trim_cps_(const std::u32string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
        while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) --b;
        return s.substr(a, b - a);
    }

    static std::string trim_utf8(const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
        while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) --b;
        return s.substr(a, b - a);
    }
    static std::u32string substr_cps_(const std::u32string& s, std::size_t off, std::size_t n) {
        if (off >= s.size()) return {};
        return s.substr(off, (n == std::u32string::npos) ? std::u32string::npos : n);
    }
    std::size_t char_to_byte_in_input_(std::size_t char_idx) const {
        std::size_t byte = 0;
        for (std::size_t i = 0; i < char_idx && i < cps.size(); ++i)
            byte += utf8_seq_len_(cps[i]);
        return byte;
    }
    static std::size_t utf8_seq_len_(char32_t c) {
        if (c <= 0x7F) return 1;
        if (c <= 0x7FF) return 2;
        if (c <= 0xFFFF) return 3;
        return 4;
    }
};

} // namespace detail

// Public entry point.
inline ParseOutput parse(const ParseInput& input) {
    record_full_document_parse();
    detail::Parser p(&input);
    auto blocks = p.parse_blocks(nullptr);
    EditorDocument doc;
    doc.root.id = NodeId(p.node_counter++);
    doc.next_node_id = p.node_counter;
    doc.dialect = input.dialect;
    doc.revision = input.revision;
    doc.root.children = std::move(blocks);
    if (!p.cps.empty() && p.cps.back() == U'\n') {
        doc.trailing_line_ending = p.cps.size() >= 2 && p.cps[p.cps.size() - 2] == U'\r'
            ? U"\r\n"
            : U"\n";
    } else if (!p.cps.empty() && p.cps.back() == U'\r') {
        doc.trailing_line_ending = U"\r";
    }

    // metadata: frontmatter first
    bool has_fm = false;
    for (const auto& b : doc.root.children) {
        if (b.kind == BlockKind::Frontmatter) {
            doc.metadata = from_frontmatter(b.atomic_special().raw, b.atomic_special().fmt);
            has_fm = true;
            break;
        }
    }
    if (!has_fm) {
        std::size_t lc = 0, wc = 0; std::u32string buf;
        auto flush = [&]() {
            if (!buf.empty()) { ++wc; buf.clear(); }
        };
        for (char32_t c : p.cps) {
            if (c == '\n') ++lc;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') flush();
            else buf.push_back(c);
        }
        flush();
        doc.metadata.char_count = p.cps.size();
        doc.metadata.line_count = lc + 1;
        doc.metadata.word_count = wc;
    }

    doc.diagnostics = std::move(p.diagnostics);

    ParseOutput out;
    out.revision = input.revision;
    out.document = std::move(doc);
    rebuild_document_block_index(out.document);
    out.symbols = build_document_symbol_index(out.document, &out.symbol_contributions);
    out.outline = build_outline_from_headings(input.revision, out.symbols.headings);
    out.diagnostics = out.document.diagnostics;
    return out;
}

inline ParseOutput parse_text(
    std::uint64_t rev,
    const std::string& text,
    MarkdownDialect d = default_dialect(),
    ParseProgressCallback progress = {}) {
    return parse(ParseInput(rev, text, d, std::move(progress)));
}

inline ParsedBlockFragment parse_block_fragment(
    std::u32string_view source,
    MarkdownDialect dialect = default_dialect()) {
    ParseInput input{0, cps_to_utf8(source), std::move(dialect)};
    detail::Parser parser(&input);
    ParsedBlockFragment result;
    result.blocks = parser.parse_blocks(nullptr);
    result.source_ranges.reserve(parser.source_ranges.size());
    for (const auto& range : parser.source_ranges) {
        result.source_ranges.push_back({
            range.node_id,
            {range.source_range.start, range.source_range.end},
            {range.content_range.start, range.content_range.end},
        });
    }
    return result;
}

} // namespace elmd

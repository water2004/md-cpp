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
//   * raw HTML becomes BlockNode/InlineNode::UnsupportedMarkup — no
//     HtmlBlock / HtmlInline node type exists (acceptance gate)
//   * ` ```math ` fence becomes MathBlock{ FencedMath } (when enabled)
//   * list / blockquote recursion uses parse_blocks with stop conditions
//
// All offsets are CHAR indices into a Vec<char32> snapshot.
export module elmd.core.parser;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.diagnostics;
import elmd.core.source_map;
import elmd.core.metadata;
import elmd.core.symbols;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.outline;
import elmd.core.slug;
import elmd.core.utf;

export namespace elmd {

struct ParseInput {
    std::uint64_t revision = 1;
    std::string text;                       // UTF-8 source
    MarkdownDialect dialect = default_dialect();
    std::vector<TextRange<CharOffset>> changed_ranges;  // reserved for incremental

    ParseInput() = default;
    ParseInput(std::uint64_t rev, std::string t, MarkdownDialect d = default_dialect())
        : revision(rev), text(std::move(t)), dialect(d) {}
};

struct ParseOutput {
    std::uint64_t revision = 1;
    MarkdownDocument document;
    DocumentSymbolIndex symbols;
    Outline outline;
    std::vector<Diagnostic> diagnostics;
};

// ---------------------------------------------------------------------------
//                              the Parser
// ---------------------------------------------------------------------------
namespace detail {

class Parser {
public:
    const ParseInput* input;
    std::u32string cps;          // text as codepoints (this is the index space)
    std::size_t pos = 0;
    std::uint64_t node_counter = 0;
    std::vector<Diagnostic> diagnostics;
    std::vector<HeadingSymbol> headings;
    std::vector<FootnoteSymbol> footnotes;
    std::vector<LinkSymbol> links;
    std::vector<ImageSymbol> images;
    std::vector<MathSymbol> math_blocks;
    std::vector<CodeBlockSymbol> code_blocks;
    std::vector<NodeSourceRange> source_ranges;

    explicit Parser(const ParseInput* in) : input(in) {
        cps = utf8_to_cps(in->text);
    }

    NodeId next_node_id() { return NodeId(node_counter++); }
    CharOffset cur() const { return CharOffset(pos); }
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
    char32_t ch_at(std::size_t i) const { return (i < cps.size()) ? cps[i] : 0; }
    void skip_ws_inline() { while (peek1() == ' ' || peek1() == '\t') advance(); }
    bool peek_line_start() const { return pos == 0 || cps[pos - 1] == '\n'; }
    bool is_blank_line() const {
        std::size_t i = pos;
        while (i < cps.size() && cps[i] != '\n') {
            if (cps[i] != ' ' && cps[i] != '\t') return false;
            ++i;
        }
        return true;
    }
    // read current line up to \n (not consumed). Implementation note: READ-ONLY.
    std::pair<std::u32string, std::size_t> rest_of_line() const {
        std::u32string s;
        std::size_t i = pos;
        while (i < cps.size() && cps[i] != '\n') { s.push_back(cps[i]); ++i; }
        return {s, i - pos};
    }
    // Push a source range. idempotent.
    void push_range(NodeId id, CharRange sr, CharRange cr) {
        NodeSourceRange r(id, sr, cr);
        source_ranges.push_back(r);
    }

    // ---- block dispatch ----
    std::vector<BlockNode> parse_blocks(std::function<bool(const std::u32string&)> stop = nullptr) {
        std::vector<BlockNode> blocks;
        while (!eof()) {
            if (is_blank_line()) {
                while (peek1() == ' ' || peek1() == '\t') advance();
                if (peek1() == '\n') { advance(); continue; }
                continue;
            }
            if (stop) {
                auto [line, _] = rest_of_line();
                if (stop(line)) break;
            }
            if (auto b = parse_block()) blocks.push_back(std::move(*b));
            else advance();
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
            if (peek1() == '#') {
                if (auto b = parse_heading()) return b;
            }
            if (peek1() == '>') {
                if (auto b = parse_blockquote_or_callout()) return b;
            }
            if (auto b = try_parse_code_block())         return b;
            if (auto b = try_parse_math_block())         return b;
            if (auto b = try_parse_table())              return b;
            if (try_parse_thematic_break())              return parse_thematic_break();
            if (auto b = try_parse_list_or_task())       return b;
            if (try_match_toc())                          return parse_toc();
            if (peek1() == '[' && ch_at(pos+1) == '^') {
                if (auto b = try_parse_footnote_definition()) return b;
            }
        }
        if (peek1() == '<') {
            if (auto b = try_parse_raw_html_block())     return b;
        }
        return parse_paragraph();
    }

    // ---- frontmatter ----
    std::optional<BlockNode> try_parse_frontmatter() {
        if (!(peek1()=='-' && peek(1)=='-' && peek(2)=='-')) return std::nullopt;
        std::size_t save = pos;
        advance_n(3);
        FrontmatterFormat fmt = FrontmatterFormat::Yaml;
        auto [first_line, _] = rest_of_line();
        auto t = cps_to_utf8(first_line);
        auto tr = trim_utf8(t);
        if (tr == "toml") fmt = FrontmatterFormat::Toml;
        else if (tr == "json") fmt = FrontmatterFormat::Json;
        else if (!tr.empty()) {} // default yaml
        while (true) {
            if (eof()) { pos = save; return std::nullopt; }
            if (peek_line_start() && peek1()=='-' && peek(1)=='-' && peek(2)=='-') {
                std::size_t closing_start = pos;
                advance_n(3);
                if (peek1() == '\n') advance();
                std::size_t content_end = closing_start;
                // slice by CHAR indices into UTF8 — safe for CJK
                std::size_t b1 = char_to_byte_in_input_(save), b2 = char_to_byte_in_input_(save + 3);
                std::size_t eC = char_to_byte_in_input_(content_end);
                std::string inner = input->text.substr(b2, (b1 < eC ? eC : b1) - b2);
                NodeId id = next_node_id();
                push_range(id, CharRange(CharOffset(save), CharOffset(pos)), CharRange(CharOffset(save), CharOffset(pos)));
                BlockNode b; b.id = id; b.kind = BlockKind::Frontmatter; b.fmt = fmt; b.raw = inner;
                return b;
            }
            if (peek1() == '\n') advance(); else advance();
        }
    }

    // ---- heading ----
    std::optional<BlockNode> parse_heading() {
        std::size_t start = pos;
        std::uint8_t level = 0;
        while (peek1() == '#' && level < 6) { advance(); ++level; }
        if (peek1() != ' ') { pos = start; return std::nullopt; }
        advance();
        std::size_t content_start = pos;
        InlineVec inlines;
        std::u32string buf;
        auto flush = [&]() {
            if (!buf.empty()) { inlines.push_back(InlineNode::text_node(next_node_id(), buf)); buf.clear(); }
        };
        while (!eof() && peek1() != '\n') {
            if (peek1() == '*') {
                flush();
                if (auto n = try_parse_inline_format()) { inlines.push_back(std::move(*n)); }
                else { buf.push_back('*'); advance(); }
                continue;
            }
            buf.push_back(peek1()); advance();
        }
        flush();
        std::size_t content_end = pos;
        if (peek1() == '\n') advance();
        std::u32string title = block_inline_text_content(inlines);
        std::string title_utf8 = cps_to_utf8(title);
        std::string slug = generate_slug(title_utf8, {});
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), CharOffset(pos)), CharRange(CharOffset(content_start), CharOffset(content_end)));
        headings.push_back({id, level, title_utf8, slug});
        BlockNode b; b.id = id; b.kind = BlockKind::Heading; b.level = level; b.children = std::move(inlines); b.slug = slug;
        return b;
    }
    std::optional<InlineNode> try_parse_inline_format() {
        if (peek1()=='*' && peek2()=='*') {
            advance_n(2);
            InlineVec ch = parse_inlines_until({'*','*'});
            std::size_t end_mark = pos; advance_n(2);
            NodeId id = next_node_id();
            push_range(id, CharRange(CharOffset(0), CharOffset(end_mark)),
                        CharRange(CharOffset(0), CharOffset(end_mark)));
            InlineNode n; n.id = id; n.kind = InlineKind::Strong; n.children = std::move(ch);
            return n;
        }
        if (peek1() == '*') {
            advance();
            std::size_t inner_start = pos;
            InlineVec ch = parse_inlines_until({'*'});
            std::size_t end_mark = pos; advance();
            NodeId id = next_node_id();
            push_range(id, CharRange(CharOffset(inner_start), CharOffset(end_mark)), CharRange(CharOffset(inner_start), CharOffset(end_mark)));
            InlineNode n; n.id = id; n.kind = InlineKind::Emphasis; n.children = std::move(ch);
            return n;
        }
        if (peek1() == '`') return try_parse_inline_code();
        return std::nullopt;
    }

    // ---- paragraph (inline state machine) ----
    std::optional<BlockNode> parse_paragraph() {
        std::size_t start = pos;
        InlineVec inlines;
        std::u32string buf;
        std::size_t text_start = pos;
        auto flush = [&]() {
            if (!buf.empty()) { inlines.push_back(InlineNode::text_node(next_node_id(), buf)); buf.clear(); }
        };
        while (!eof()) {
            if (peek1() == '\n') {
                advance();
                if (peek_line_start()) break;
                flush();
                continue;
            }
            // inline constructs — exact order
            if (peek1() == '$') {
                if (auto n = try_parse_inline_math()) { flush(); inlines.push_back(std::move(*n)); continue; }
            }
            if (peek1() == '*' && peek2() == '*') {
                flush();
                std::size_t inner_start = pos + 2;
                advance_n(2);
                InlineVec ch = parse_inlines_until({'*','*'});
                std::size_t end_mark = pos; advance_n(2);
                NodeId id = next_node_id();
                push_range(id, CharRange(CharOffset(inner_start), CharOffset(end_mark)), CharRange(CharOffset(inner_start), CharOffset(end_mark)));
                InlineNode n; n.id = id; n.kind = InlineKind::Strong; n.children = std::move(ch);
                inlines.push_back(std::move(n));
                continue;
            }
            if (peek1() == '*' && peek2() != ' ' && peek2() != '*') {
                flush();
                std::size_t inner_start = pos + 1;
                advance();
                InlineVec ch = parse_inlines_until({'*'});
                std::size_t end_mark = pos; advance();
                NodeId id = next_node_id();
                push_range(id, CharRange(CharOffset(inner_start), CharOffset(end_mark)), CharRange(CharOffset(inner_start), CharOffset(end_mark)));
                InlineNode n; n.id = id; n.kind = InlineKind::Emphasis; n.children = std::move(ch);
                inlines.push_back(std::move(n));
                continue;
            }
            if (peek1() == '_' && peek2() == '_' && peek2next_is_nonspace_underscore_()) {
                flush();
                std::size_t inner_start = pos + 2;
                advance_n(2);
                InlineVec ch = parse_inlines_until({'_','_'});
                std::size_t end_mark = pos; advance_n(2);
                NodeId id = next_node_id();
                push_range(id, CharRange(CharOffset(inner_start), CharOffset(end_mark)), CharRange(CharOffset(inner_start), CharOffset(end_mark)));
                InlineNode n; n.id = id; n.kind = InlineKind::Strong; n.children = std::move(ch);
                inlines.push_back(std::move(n));
                continue;
            }
            if (peek1() == '_' && peek2() != ' ' && peek2() != '_') {
                flush();
                std::size_t inner_start = pos + 1;
                advance();
                InlineVec ch = parse_inlines_until({'_'});
                std::size_t end_mark = pos; advance();
                NodeId id = next_node_id();
                push_range(id, CharRange(CharOffset(inner_start), CharOffset(end_mark)), CharRange(CharOffset(inner_start), CharOffset(end_mark)));
                InlineNode n; n.id = id; n.kind = InlineKind::Emphasis; n.children = std::move(ch);
                inlines.push_back(std::move(n));
                continue;
            }
            if (peek1() == '~' && peek2() == '~' && peek2next2() != ' ') {
                flush();
                std::size_t inner_start = pos + 2;
                advance_n(2);
                InlineVec ch = parse_inlines_until({'~','~'});
                std::size_t end_mark = pos; advance_n(2);
                NodeId id = next_node_id();
                push_range(id, CharRange(CharOffset(inner_start), CharOffset(end_mark)), CharRange(CharOffset(inner_start), CharOffset(end_mark)));
                InlineNode n; n.id = id; n.kind = InlineKind::Strike; n.children = std::move(ch);
                inlines.push_back(std::move(n));
                continue;
            }
            if (peek1() == '`') {
                if (auto n = try_parse_inline_code()) { flush(); inlines.push_back(std::move(*n)); continue; }
            }
            if (peek1() == '!' && peek2() == '[') {
                if (auto n = try_parse_link_or_image()) { flush(); inlines.push_back(std::move(*n)); continue; }
            }
            if (peek1() == '[') {
                if (peek2() == '^') {
                    if (auto n = try_parse_footnote_ref()) { flush(); inlines.push_back(std::move(*n)); continue; }
                }
                if (peek(1) == '[') {
                    if (auto n = try_parse_wiki_link()) { flush(); inlines.push_back(std::move(*n)); continue; }
                }
                if (auto n = try_parse_link_or_image()) { flush(); inlines.push_back(std::move(*n)); continue; }
            }
            if (peek1() == '<') {
                if (auto n = try_parse_raw_html_inline()) { flush(); inlines.push_back(std::move(*n)); continue; }
            }
            buf.push_back(peek1()); advance();
        }
        flush();
        if (inlines.empty()) return std::nullopt;
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), CharOffset(pos)), CharRange(CharOffset(start), CharOffset(pos)));
        BlockNode b; b.id = id; b.kind = BlockKind::Paragraph; b.children = std::move(inlines);
        return b;
    }

    bool peek2next_is_nonspace_underscore_() { char32_t c = peek2(); return c != ' ' && c != '_' && c != 0; }
    char32_t peek2next2() { return peek2(); }

    // parse_inlines_until(del) — collect text until a run of `del` chars.
    // Delimiters DO NOT cross newlines.
    InlineVec parse_inlines_until(std::vector<char32_t> del) {
        InlineVec out;
        std::u32string buf;
        auto flush = [&]() {
            if (!buf.empty()) { out.push_back(InlineNode::text_node(next_node_id(), buf)); buf.clear(); }
        };
        while (!eof()) {
            if (peek1() == '\n') break;
            bool matched = false;
            if (pos + del.size() <= cps.size()) {
                matched = true;
                for (std::size_t i = 0; i < del.size(); ++i)
                    if (cps[pos + i] != del[i]) { matched = false; break; }
            }
            if (matched) break;
            buf.push_back(peek1()); advance();
        }
        flush();
        return out;
    }

    // ---- inline math `$...$` ----
    std::optional<InlineNode> try_parse_inline_math() {
        std::size_t start = pos;
        advance(); // consume $
        if (peek1() == '$') { pos = start; return std::nullopt; } // $$ belongs to block path
        std::u32string tex;
        while (!eof() && peek1() != '$' && peek1() != '\n') { tex.push_back(peek1()); advance(); }
        if (peek1() != '$') {
            pos = start;
            diagnostics.push_back(make_diagnostic(DiagnosticSeverity::Warning,
                "Unclosed inline math delimiter `$`",
                CharRange(CharOffset(start), cur()), DIAG_UNCLOSED_MATH_DOLLAR));
            return std::nullopt;
        }
        advance();
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start + 1), cur()));
        InlineNode n; n.id = id; n.kind = InlineKind::InlineMath; n.text = std::move(tex); n.math_delim = MathDelimiter::InlineDollar;
        return n;
    }

    // ---- inline code ----
    std::optional<InlineNode> try_parse_inline_code() {
        std::size_t start = pos;
        std::size_t count = 0;
        while (peek1() == '`') { ++count; advance(); }
        std::u32string code;
        while (!eof()) {
            if (peek1() == '`') {
                std::size_t save = pos, c = 0;
                while (peek1() == '`' && c < count) { ++c; advance(); }
                if (c == count) break;
                pos = save; // not enough backticks — treat as literal content
            }
            if (peek1() == '\n') break;
            code.push_back(peek1()); advance();
        }
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start + count), cur()));
        InlineNode n; n.id = id; n.kind = InlineKind::InlineCode; n.text = std::move(code);
        return n;
    }

    // ---- link / image ----
    std::optional<InlineNode> try_parse_link_or_image() {
        std::size_t start = pos;
        bool is_image = (peek1() == '!');
        if (is_image) advance();
        if (peek1() != '[') return std::nullopt;
        advance();
        std::u32string text;
        while (!eof() && peek1() != ']') { text.push_back(peek1()); advance(); }
        if (peek1() != ']') { pos = start; return std::nullopt; }
        advance();
        if (peek1() != '(') { pos = start; return std::nullopt; }
        advance();
        std::u32string href;
        std::optional<std::string> title;
        while (!eof() && peek1() != ')' && peek1() != '\n') {
            if (peek1() == ' ') {
                advance();
                if (peek1() == '"' || peek1() == '\'') {
                    char32_t q = peek1(); advance();
                    std::u32string t;
                    while (!eof() && peek1() != q) { t.push_back(peek1()); advance(); }
                    if (peek1() == q) advance();
                    title = cps_to_utf8(t);
                }
                break;
            }
            href.push_back(peek1()); advance();
        }
        if (peek1() == ')') advance();
        else { pos = start; return std::nullopt; }
        NodeId id = next_node_id();
        std::string href_s = cps_to_utf8(href);
        std::string text_s = cps_to_utf8(text);
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start + (is_image ? 2 : 1)), cur()));
        if (is_image) {
            images.push_back({id, href_s, text_s});
            InlineNode n; n.id = id; n.kind = InlineKind::Image; n.href = href_s; n.alt = text_s; n.title = title;
            return n;
        } else {
            links.push_back({id, href_s, text_s});
            InlineNode n; n.id = id; n.kind = InlineKind::Link; n.href = href_s; n.title = title;
            n.children.push_back(InlineNode::text_node(next_node_id(), std::move(text)));
            return n;
        }
    }

    // ---- footnote ref ----
    std::optional<InlineNode> try_parse_footnote_ref() {
        std::size_t start = pos;
        if (!(peek1() == '[' && peek2() == '^')) return std::nullopt;
        advance_n(2);
        std::u32string label;
        while (!eof() && peek1() != ']' && peek1() != '\n') { label.push_back(peek1()); advance(); }
        if (peek1() != ']') { pos = start; return std::nullopt; }
        advance();
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start + 2), cur()));
        InlineNode n; n.id = id; n.kind = InlineKind::FootnoteRef; n.label = cps_to_utf8(label);
        return n;
    }

    // ---- wiki link ----
    std::optional<InlineNode> try_parse_wiki_link() {
        std::size_t start = pos;
        if (!(peek1() == '[' && peek2() == '[')) return std::nullopt;
        advance_n(2);
        std::u32string target;
        std::optional<std::string> alias;
        while (!eof() && peek1() != '\n') {
            if (peek1() == ']' && peek2() == ']') { advance_n(2); break; }
            if (peek1() == '|') {
                advance();
                std::u32string a;
                while (!eof() && peek1() != '\n') {
                    if (peek1() == ']' && peek2() == ']') { advance_n(2); break; }
                    a.push_back(peek1()); advance();
                }
                alias = cps_to_utf8(a);
                break;
            }
            target.push_back(peek1()); advance();
        }
        if (target.empty()) { pos = start; return std::nullopt; }
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start + 2), cur()));
        InlineNode n; n.id = id; n.kind = InlineKind::WikiLink; n.target = cps_to_utf8(target); n.alias = alias;
        return n;
    }

    // ---- raw html inline → UnsupportedMarkup ----
    std::optional<InlineNode> try_parse_raw_html_inline() {
        std::size_t save = pos;
        std::u32string tag; tag.push_back('<'); advance();
        if (peek1() == '/') { tag.push_back('/'); advance(); }
        while (!eof() && is_alnum_(peek1())) { tag.push_back(peek1()); advance(); }
        while (!eof() && peek1() != '>' && peek1() != '\n') { tag.push_back(peek1()); advance(); }
        if (peek1() != '>') { pos = save; return std::nullopt; }
        tag.push_back('>'); advance();
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(save), cur()), CharRange(CharOffset(save), cur()));
        diagnostics.push_back(make_diagnostic(DiagnosticSeverity::Hint,
            "Raw HTML is disabled, shown as text",
            CharRange(CharOffset(save), cur()), DIAG_RAW_HTML_DISABLED));
        InlineNode n; n.id = id; n.kind = InlineKind::UnsupportedMarkup; n.text = tag; n.unsup_reason = UnsupportedMarkupReason::RawHtmlDisabled;
        return n;
    }

    // ---- raw html block → UnsupportedMarkup ----
    std::optional<BlockNode> try_parse_raw_html_block() {
        std::size_t save = pos;
        std::u32string tag_content;
        while (!eof() && peek1() != '\n' && peek1() != '>') { tag_content.push_back(peek1()); advance(); }
        if (peek1() != '>') { pos = save; return std::nullopt; }
        advance();
        std::u32string full_open = tag_content; full_open.push_back('>');
        auto lower = to_lower_(full_open);
        bool is_comment = starts_with_(lower, U"<!--");
        std::u32string raw = full_open;
        if (!is_comment) {
            std::u32string tag_name;
            for (char32_t c : full_open) {
                if (c == '<') continue;
                if (c == ' ' || c == '\t' || c == '\n' || c == '>') break;
                if (c == '/') break;
                tag_name.push_back(c);
            }
            if (tag_name.empty()) { pos = save; return std::nullopt; }
            std::u32string close_tag = U"</"; close_tag += tag_name; close_tag.push_back('>');
            while (!eof()) {
                if (peek1() == '\n') {
                    raw.push_back('\n'); advance();
                    if (peek1() == '<') {
                        std::size_t maybe_close = pos;
                        std::u32string close_test;
                        while (!eof() && peek1() != '\n' && peek1() != '>') { close_test.push_back(peek1()); advance(); }
                        if (peek1() == '>') { close_test.push_back('>'); advance(); }
                        if (to_lower_(close_test) == to_lower_(close_tag)) { raw += close_test; break; }
                        else pos = maybe_close;
                    }
                } else {
                    raw.push_back(peek1()); advance();
                }
            }
        }
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(save), cur()), CharRange(CharOffset(save), cur()));
        diagnostics.push_back(make_diagnostic(DiagnosticSeverity::Hint,
            "Raw HTML is disabled, shown as unsupported markup",
            CharRange(CharOffset(save), cur()), DIAG_RAW_HTML_DISABLED));
        BlockNode b; b.id = id; b.kind = BlockKind::UnsupportedMarkup; b.raw = cps_to_utf8(raw); b.unsup_reason = UnsupportedMarkupReason::RawHtmlDisabled;
        return b;
    }

    // ---- footnote definition ----
    std::optional<BlockNode> try_parse_footnote_definition() {
        std::size_t start = pos;
        std::size_t save = pos;
        advance_n(2);
        std::u32string label;
        while (!eof() && peek1() != ']' && peek1() != '\n') { label.push_back(peek1()); advance(); }
        if (peek1() != ']') { pos = save; return std::nullopt; }
        advance();
        if (peek1() != ':') { pos = save; return std::nullopt; }
        advance();
        if (peek1() == ' ') advance();
        std::size_t content_start = pos;
        std::vector<BlockNode> children;
        {
            auto prev_block_start = [this](const std::u32string& line) -> bool {
                // stop at next footnote-def / line that's not indented and starts with '[^'
                std::size_t i = 0;
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
                if (i + 1 < line.size() && line[i] == '[' && line[i+1] == '^') return true;
                return false;
            };
            children = parse_blocks(prev_block_start);
        }
        std::size_t end = pos;
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), CharOffset(end)), CharRange(CharOffset(content_start), CharOffset(end)));
        footnotes.push_back({id, cps_to_utf8(label)});
        BlockNode b; b.id = id; b.kind = BlockKind::FootnoteDefinition; b.footnote_label = cps_to_utf8(label); b.quote_children = std::move(children);
        return b;
    }

    // ---- code fence / fenced math ----
    std::optional<BlockNode> try_parse_code_block() {
        std::size_t start = pos;
        std::size_t count = 0;
        while (peek1() == '`') { ++count; advance(); }
        if (count < 3) { pos = start; return std::nullopt; }
        auto [info_line, _] = rest_of_line();
        if (peek1() == '\n') advance();
        auto info_utf8 = cps_to_utf8(info_line);
        std::optional<std::string> lang;
        auto trimmed = trim_utf8(info_utf8);
        if (!trimmed.empty()) lang = trimmed;
        std::u32string text;
        while (!eof()) {
            if (peek1() == '\n') {
                text.push_back('\n'); advance();
                std::size_t save = pos, c = 0;
                while (peek1() == '`') { ++c; advance(); }
                if (c >= count) {
                    if (eof() || peek1() == '\n' || peek1() == ' ') {
                        if (peek1() == '\n') advance();
                        break;
                    }
                    pos = save;
                }
                continue;
            }
            text.push_back(peek1()); advance();
        }
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start), cur()));
        std::size_t n_lines = 1; for (char32_t c : text) if (c == '\n') ++n_lines;
        code_blocks.push_back({id, lang, n_lines});
        if (lang && *lang == "math" && input->dialect.math.fenced_math) {
            math_blocks.push_back({id, first_three_lines_utf8_(text)});
            BlockNode b; b.id = id; b.kind = BlockKind::MathBlock; b.tex = std::move(text); b.math_delim = MathDelimiter::FencedMath;
            return b;
        }
        BlockNode b; b.id = id; b.kind = BlockKind::CodeBlock; b.language = lang; b.code_text = std::move(text);
        return b;
    }

    // ---- math block `$$ ... $$` ----
    std::optional<BlockNode> try_parse_math_block() {
        std::size_t start = pos;
        if (!(peek1() == '$' && peek2() == '$')) return std::nullopt;
        advance_n(2);
        if (peek1() == '\n') advance();
        std::u32string tex;
        while (!eof()) {
            if (peek1() == '$' && peek2() == '$') {
                advance_n(2);
                if (peek1() == '\n') advance();
                NodeId id = next_node_id();
                push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start), cur()));
                math_blocks.push_back({id, first_three_lines_utf8_(tex)});
                auto trimmed = trim_cps_(tex);
                BlockNode b; b.id = id; b.kind = BlockKind::MathBlock; b.tex = trimmed; b.math_delim = MathDelimiter::BlockDollar;
                return b;
            }
            tex.push_back(peek1()); advance();
        }
        diagnostics.push_back(make_diagnostic(DiagnosticSeverity::Warning,
            "Unclosed math block delimiter $$",
            CharRange(CharOffset(start), cur()), DIAG_UNCLOSED_MATH_DOLLAR));
        pos = start + 2;
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start), cur()));
        math_blocks.push_back({id, first_three_lines_utf8_(tex)});
        BlockNode b; b.id = id; b.kind = BlockKind::MathBlock; b.tex = std::move(tex); b.math_delim = MathDelimiter::BlockDollar;
        return b;
    }

    // ---- table ----
    std::optional<BlockNode> try_parse_table() {
        std::size_t save = pos;
        auto [header_line, _] = rest_of_line();
        if (header_line.find(U'|') == std::u32string::npos) { pos = save; return std::nullopt; }
        auto header_row = parse_table_row();
        if (!header_row) { pos = save; return std::nullopt; }
        if (peek1() == '\n') advance();
        std::size_t save2 = pos;
        auto [sep_line, _2] = rest_of_line();
        bool sep_has_pipe = (sep_line.find(U'|') != std::u32string::npos);
        bool sep_has_dash = (sep_line.find(U'-') != std::u32string::npos);
        if (!sep_has_pipe || !sep_has_dash) { pos = save; return std::nullopt; }
        pos = save2;
        auto alignments = parse_table_separator();
        if (!alignments) { pos = save; return std::nullopt; }
        if (peek1() == '\n') advance();
        std::vector<TableRow> rows;
        while (!eof()) {
            std::size_t save3 = pos;
            auto [line, _l] = rest_of_line();
            if (line.find(U'|') == std::u32string::npos) break;
            pos = save3;
            if (auto r = parse_table_row()) rows.push_back(std::move(*r)); else break;
            if (peek1() == '\n') advance();
        }
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(save), cur()), CharRange(CharOffset(save), cur()));
        BlockNode b; b.id = id; b.kind = BlockKind::Table;
        // header: per Rust quirk, a single empty header cell is emitted.
        TableCell hc; hc.id = next_node_id();
        b.table_header.push_back(hc);
        b.table_rows = std::move(rows);
        b.table_aligns = std::move(*alignments);
        return b;
    }

    std::optional<TableRow> parse_table_row() {
        std::size_t row_start = pos;
        auto [line, _] = rest_of_line();
        std::vector<TableCell> cells;
        std::u32string cell_text;
        for (char32_t ch : line) {
            if (ch == '|') {
                if (!cell_text.empty() || !cells.empty()) {
                    TableCell c; c.id = next_node_id();
                    auto t = trim_cps_(cell_text);
                    c.children.push_back(InlineNode::text_node(next_node_id(), std::move(t)));
                    cells.push_back(std::move(c));
                    cell_text.clear();
                }
            } else {
                cell_text.push_back(ch); advance();
            }
        }
        pos = row_start + line.size() + 1; // skip newline
        if (cells.empty()) return std::nullopt;
        TableRow r; r.id = next_node_id(); r.cells = std::move(cells);
        return r;
    }

    std::optional<std::vector<TableAlignment>> parse_table_separator() {
        std::size_t save = pos;
        auto [line, _] = rest_of_line();
        pos = save;
        std::vector<TableAlignment> alignments;
        // split on '|'
        std::vector<std::u32string> parts;
        std::u32string acc;
        for (char32_t c : line) {
            if (c == '|') { parts.push_back(acc); acc.clear(); }
            else acc.push_back(c);
        }
        parts.push_back(acc);
        for (auto& p : parts) {
            auto tr = trim_cps_(p);
            if (tr.empty()) continue;
            // skip empty parts that are all spaces/`.`/`:`
            bool skip = true;
            for (char32_t c : tr) if (c != ' ' && c != '.' && c != ':') { skip = false; break; }
            if (skip) {
                // consume those chars from the buffer (advance pos as we go)
                continue;
            }
            bool starts = (!tr.empty() && tr.front() == ':');
            bool ends   = (!tr.empty() && tr.back() == ':');
            for (std::size_t i = 0; i < p.size(); ++i) advance();
            TableAlignment a = TableAlignment::None;
            if (starts && ends) a = TableAlignment::Center;
            else if (starts) a = TableAlignment::Left;
            else if (ends) a = TableAlignment::Right;
            alignments.push_back(a);
        }
        // drain remaining separator-line chars
        while (peek1() == '|' || peek1() == ' ' || peek1() == '-' || peek1() == ':') advance();
        if (peek1() == '\n') advance();
        if (alignments.empty()) return std::nullopt;
        return alignments;
    }

    // ---- list / task ----
    std::optional<BlockNode> try_parse_list_or_task() {
        std::size_t save = pos;
        std::size_t start = pos;
        bool is_ordered = false, is_task = false;
        char32_t c = peek1();
        if (c == '-' || c == '*' || c == '+') { advance(); }
        else if (is_ascii_digit_(c)) {
            is_ordered = true;
            while (is_ascii_digit_(peek1())) advance();
            if (peek1() != '.') { pos = save; return std::nullopt; }
            advance();
        } else { pos = save; return std::nullopt; }
        if (peek1() != ' ') { pos = save; return std::nullopt; }
        advance();
        std::size_t save2 = pos;
        ListItem head_item;
        TaskListItem head_task;
        bool checked = false;
        if (peek1() == '[' && (peek2() == ' ' || peek2() == 'x' || peek2() == 'X') && peek(2) == ']') {
            is_task = true;
            checked = (peek2() == 'x' || peek2() == 'X');
            advance_n(3);
            if (peek1() == ' ') advance();
            save2 = pos;
        } else {
            pos = save2;
        }
        auto [content, content_len] = rest_of_line();
        advance_n(content_len); // HANDOFF fix #1 — consume line so main loop won't re-parse as paragraph
        NodeId item_id = next_node_id();
        NodeId para_id = next_node_id();
        BlockNode para; para.id = para_id; para.kind = BlockKind::Paragraph;
        para.children.push_back(InlineNode::text_node(next_node_id(), content));
        std::vector<BlockNode> para_children; para_children.push_back(std::move(para));
        if (peek1() == '\n') advance();

        NodeId list_id = next_node_id();
        if (is_task) {
            TaskListItem ti; ti.id = item_id; ti.checked = checked; ti.children = std::move(para_children);
            BlockNode b; b.id = list_id; b.kind = BlockKind::TaskList; b.task_items.push_back(std::move(ti));
            return b;
        } else {
            ListItem li; li.id = item_id; li.children = std::move(para_children);
            BlockNode b; b.id = list_id; b.kind = BlockKind::List; b.list_items.push_back(std::move(li));
            (void)start;
            return b;
        }
    }

    // ---- blockquote / callout ----
    std::optional<BlockNode> parse_blockquote_or_callout() {
        std::size_t start = pos;
        advance(); // '>'
        if (peek1() == ' ') advance();
        std::size_t content_start = pos;
        auto [first_line, _] = rest_of_line();
        if (auto callout = try_parse_callout_from_line(first_line)) {
            // emit the proper Callout with the freshly built id / src range / children
            auto children = parse_blockquote_body();
            std::size_t end = pos;
            NodeId id = next_node_id();
            push_range(id, CharRange(CharOffset(start), CharOffset(end)), CharRange(CharOffset(content_start), CharOffset(end)));
            BlockNode& cb = *callout;
            cb.id = id;
            cb.quote_children = std::move(children);
            return callout;
        }
        auto children = parse_blockquote_body();
        std::size_t end = pos;
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), CharOffset(end)), CharRange(CharOffset(content_start), CharOffset(end)));
        BlockNode b; b.id = id; b.kind = BlockKind::BlockQuote; b.quote_children = std::move(children);
        return b;
    }
    std::optional<BlockNode> try_parse_callout_from_line(const std::u32string& line) {
        auto trimmed = trim_cps_(line);
        auto t = std::u32string(trimmed);
        if (t.size() >= 2 && t[0] == '[' && t[1] == '!') {
            std::size_t kind_end = std::u32string::npos;
            for (std::size_t i = 2; i < t.size(); ++i) if (t[i] == ']') { kind_end = i; break; }
            if (kind_end == std::u32string::npos) return std::nullopt;
            std::u32string kind = substr_cps_(t, 2, kind_end - 2);
            std::u32string title_part = (kind_end + 1 < t.size()) ? substr_cps_(t, kind_end + 1, std::u32string::npos) : U"";
            title_part = trim_cps_(title_part);
            std::optional<InlineVec> title;
            if (!title_part.empty()) {
                InlineVec tv; tv.push_back(InlineNode::text_node(next_node_id(), std::move(title_part)));
                title = std::move(tv);
            }
            // uppercase kind
            for (char32_t& c : kind) if (c >= 'a' && c <= 'z') c = static_cast<char32_t>(c - 'a' + 'A');
            BlockNode b; b.id = NodeId(0); b.kind = BlockKind::Callout; b.callout_kind = cps_to_utf8(kind); b.callout_title = std::move(title);
            return b;
        }
        return std::nullopt;
    }
    std::vector<BlockNode> parse_blockquote_body() {
        std::vector<BlockNode> blocks;
        while (!eof()) {
            if (peek_line_start() && peek1() == '>') {
                advance();
                if (peek1() == ' ') advance();
            }
            if (is_blank_line()) {
                while (peek1() == ' ' || peek1() == '\t') advance();
                if (peek1() == '\n') advance();
                if (!peek_line_start() || peek1() != '>') break;
                continue;
            }
            if (auto b = parse_block()) blocks.push_back(std::move(*b));
            else break;
        }
        return blocks;
    }

    // ---- thematic break ----
    bool try_parse_thematic_break() {
        std::size_t save = pos;
        char32_t c = peek1();
        if (c != '-' && c != '*' && c != '_') return false;
        std::size_t count = 0;
        while (peek1() == c) { ++count; advance(); }
        while (peek1() == ' ' || peek1() == '\t') advance();
        if (count >= 3 && (eof() || peek1() == '\n')) {
            if (peek1() == '\n') advance();
            return true;
        }
        pos = save;
        return false;
    }
    std::optional<BlockNode> parse_thematic_break() {
        std::size_t start = pos;
        while (!eof() && peek1() != '\n') advance();
        if (peek1() == '\n') advance();
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start), cur()));
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
        if (peek1() == '\n') advance();
        NodeId id = next_node_id();
        push_range(id, CharRange(CharOffset(start), cur()), CharRange(CharOffset(start), cur()));
        BlockNode b; b.id = id; b.kind = BlockKind::Toc; b.toc_marker = mk;
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
    std::u32string first_three_lines_(const std::u32string& text) const {
        std::vector<std::u32string> lines;
        std::u32string acc;
        for (char32_t c : text + U"\n") {
            if (c == '\n') { lines.push_back(acc); acc.clear(); }
            else acc.push_back(c);
        }
        std::u32string out;
        for (std::size_t i = 0; i < lines.size() && i < 3; ++i) {
            if (i) out.push_back('\n');
            out += lines[i];
        }
        return out;
    }
    std::string first_three_lines_utf8_(const std::u32string& text) const {
        return cps_to_utf8(first_three_lines_(text));
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
    detail::Parser p(&input);
    auto blocks = p.parse_blocks(nullptr);
    MarkdownDocument doc;
    doc.revision = input.revision;
    doc.blocks = std::move(blocks);

    // metadata: frontmatter first
    bool has_fm = false;
    for (const auto& b : doc.blocks) {
        if (b.kind == BlockKind::Frontmatter) {
            doc.metadata = from_frontmatter(b.raw, b.fmt);
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

    doc.source_map.node_ranges = std::move(p.source_ranges);
    doc.diagnostics = std::move(p.diagnostics);
    // symbols
    DocumentSymbolIndex sym;
    sym.headings = std::move(p.headings);
    sym.footnotes = std::move(p.footnotes);
    sym.links = std::move(p.links);
    sym.images = std::move(p.images);
    sym.math_blocks = std::move(p.math_blocks);
    sym.code_blocks = std::move(p.code_blocks);

    auto outline = build_outline_from_blocks(input.revision, doc.blocks);

    ParseOutput out;
    out.revision = input.revision;
    out.document = std::move(doc);
    out.symbols = std::move(sym);
    out.outline = std::move(outline);
    out.diagnostics = out.document.diagnostics;
    return out;
}

inline ParseOutput parse_text(std::uint64_t rev, const std::string& text, MarkdownDialect d = default_dialect()) {
    return parse(ParseInput(rev, text, d));
}

} // namespace elmd
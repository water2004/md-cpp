// elmd.core.inline_parser — lossless editable inline CST parser.
//
// Parses a content node's `InlineDocument::source` into an `InlineCstTree` such
// that `flatten_tokens(tree, source) == source` character-for-character. Every
// source character belongs to exactly one top-level node (and top-level ranges
// are contiguous and exhaustive over `source`).
//
// Unlike a semantic Markdown parser, this one is *lossless and editable*:
//   - Unclosed constructs (`**`, `` ` ``, `[title](`, `$abc`, `~~abc`) are kept
//     as Incomplete nodes spanning their opener + trailing content, NOT dropped
//     or collapsed to plain text. The opener source is preserved verbatim.
//   - Ambiguous delimiter runs are kept as Delimiter/Error nodes.
//   - Markers, escapes, entities, whitespace, link URL spellings and quote
//     forms are all preserved as source ranges.
//
// The parser is block-local: it sees only one node's inline source and never
// crosses a block boundary. Soft/hard breaks are within-node '\n'.
export module elmd.core.inline_parser;
import std;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.utf;
import elmd.core.inline_cst;
import elmd.core.inline_document;

export namespace elmd {

namespace inline_parser_detail {

struct Parser {
    std::u32string_view src;
    std::size_t pos = 0;
    std::size_t limit = 0;
    InlineParseContext ctx;
    std::uint64_t counter;

    explicit Parser(std::u32string_view s, const InlineParseContext& c)
        : src(s), limit(s.size()), ctx(c), counter(c.next_id.v) {}

    NodeId next_id() { return ctx.allocate_id ? ctx.allocate_id() : NodeId(counter++); }
    std::size_t size() const { return limit; }
    bool eof() const { return pos >= limit; }
    char32_t peek(std::size_t k = 0) const { return pos + k < limit ? src[pos + k] : char32_t{0}; }
    char32_t ch(std::size_t i) const { return i < limit ? src[i] : char32_t{0}; }
    void advance(std::size_t n = 1) { pos = (std::min)(pos + n, limit); }

    static bool ascii_punct(char32_t c) {
        return (c >= U'!' && c <= U'/') || (c >= U':' && c <= U'@') || (c >= U'[' && c <= U'`') || (c >= U'{' && c <= U'~');
    }

    // Match a literal substring at `pos`; return true and advance if matched.
    bool match(std::u32string_view lit) {
        if (pos + lit.size() > limit) return false;
        for (std::size_t i = 0; i < lit.size(); ++i) if (src[pos + i] != lit[i]) return false;
        advance(lit.size());
        return true;
    }

    // Find the next occurrence of `lit` from `from`, not crossing a '\n'.
    static std::size_t find_on_line(std::u32string_view s, std::size_t from, std::u32string_view lit) {
        for (std::size_t i = from; i + lit.size() <= s.size(); ++i) {
            if (s[i] == U'\n') return npos;
            bool ok = true;
            for (std::size_t j = 0; j < lit.size(); ++j) if (s[i + j] != lit[j]) { ok = false; break; }
            if (ok) return i;
        }
        return npos;
    }
    static constexpr std::size_t npos = std::u32string::npos;

    // ---- entity (preserved verbatim as an Entity node) ----
    // Returns end offset (exclusive) of a recognized entity starting at `at`, else npos.
    static std::size_t entity_end(std::u32string_view s, std::size_t at) {
        if (at >= s.size() || s[at] != U'&') return npos;
        std::size_t end = at + 1;
        while (end < s.size() && end - at <= 12 && s[end] != U';' && s[end] != U'\n') ++end;
        if (end >= s.size() || s[end] != U';') return npos;
        auto body = cps_to_utf8(s.substr(at + 1, end - at - 1));
        if (body == "amp" || body == "lt" || body == "gt" || body == "quot" || body == "apos"
            || body == "nbsp" || body == "#39" || body.starts_with("#")) {
            return end + 1;
        }
        return npos;
    }

    // ---- leaf text run accumulator ----
    // Emit any pending literal run [run_start, pos) as a single Text node.
    void flush_text_to(InlineCstNodes& out, std::size_t& run_start, std::size_t end) {
        if (end <= run_start) return;
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::Text;
        node.range = {run_start, end};
        out.push_back(std::move(node));
        run_start = end;
    }

    void flush_text(InlineCstNodes& out, std::size_t& run_start) {
        flush_text_to(out, run_start, pos);
    }

    // ---- soft / hard break at a '\n' ----
    // Returns true if a break node was emitted (consuming through the newline).
    bool parse_break(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() != U'\n') return false;
        std::size_t nl = pos;
        // Hard break: two trailing spaces (already in the text run) — detect by
        // looking back into the pending run.
        bool hard = false;
        if (pos >= 2 && ch(pos - 1) == U' ' && ch(pos - 2) == U' ') {
            hard = true;
        } else if (pos >= 1 && ch(pos - 1) == U'\\') {
            // backslash line break "\\\n"
            hard = true;
        }
        if (hard) {
            // Emit preceding text (if any) excluding the trailing break chars,
            // then a HardBreak node covering the break source.
            std::size_t break_start = pos;
            std::size_t text_end = pos;
            if (ch(pos - 1) == U'\\') {
                break_start = pos - 1;
                text_end = pos - 1;
            } else {
                break_start = pos - 2;
                text_end = pos - 2;
            }
            // An incomplete construct immediately before this newline may
            // already own the trailing backslash/spaces. Top-level nodes must
            // not claim those characters twice.
            break_start = (std::max)(break_start, run_start);
            if (text_end > run_start) {
                InlineCstNode node;
                node.id = next_id();
                node.kind = InlineCstKind::Text;
                node.range = {run_start, text_end};
                out.push_back(std::move(node));
            }
            run_start = pos;
            advance(); // consume '\n'
            InlineCstNode hb;
            hb.id = next_id();
            hb.kind = InlineCstKind::HardBreak;
            hb.range = {break_start, pos};
            hb.delim = {{break_start, pos}, {break_start, pos}, {break_start, pos}, std::nullopt};
            out.push_back(std::move(hb));
            run_start = pos;
        } else {
            flush_text(out, run_start);
            advance();
            InlineCstNode sb;
            sb.id = next_id();
            sb.kind = InlineCstKind::SoftBreak;
            sb.range = {nl, pos};
            sb.delim = {{nl, pos}, {nl, pos}, {nl, pos}, std::nullopt};
            out.push_back(std::move(sb));
            run_start = pos;
        }
        return true;
    }

    // ---- escape `\punct` ----
    bool parse_escape(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() != U'\\') return false;
        if (peek(1) == U'\n') return false; // handled as hard break
        if (!ascii_punct(peek(1))) return false;
        std::size_t start = pos;
        flush_text_to(out, run_start, start);
        advance(2);
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::Escape;
        node.range = {start, pos};
        node.delim = {{start, pos}, {start, pos}, {start, pos}, std::nullopt};
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    // ---- entity &amp; ----
    bool parse_entity(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() != U'&') return false;
        std::size_t end = entity_end(src, pos);
        if (end == npos) return false;
        std::size_t start = pos;
        flush_text_to(out, run_start, start);
        pos = end;
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::Entity;
        node.range = {start, pos};
        node.delim = {{start, pos}, {start, pos}, {start, pos}, std::nullopt};
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    // ---- delimited inline (emphasis/strong/strike) ----
    // Tries to parse a `**...**`, `*...*`, `__...__`, `_..._`, `~~...~~`.
    // On a missing closer, emits an Incomplete node covering the opener + the
    // remainder of the line (still lossless — every char accounted for).
    bool parse_delimited(InlineCstNodes& out, std::size_t& run_start, std::u32string_view opener, InlineCstKind kind) {
        if (!starts_with(pos, opener)) return false;
        // left-flanking / right-flanking sanity: opener must not be followed by
        // whitespace; for `_` also not followed by another `_`.
        std::size_t after = pos + opener.size();
        if (after >= limit) {
            const auto start = pos;
            flush_text_to(out, run_start, start);
            pos = after;
            InlineCstNode node;
            node.id = next_id();
            node.kind = InlineCstKind::Incomplete;
            node.status = ParseStatus::MissingCloser;
            node.range = {start, pos};
            node.delim = {{start, pos}, {start, pos}, {pos, pos}, std::nullopt};
            out.push_back(std::move(node));
            run_start = pos;
            return true;
        }
        char32_t next = ch(after);
        if (next == U' ' || next == U'\n' || next == U'\t') return false;
        if (opener == U"_" && next == U'_') return false;
        if (opener == U"~~" && ch(after) == U' ') return false;

        std::size_t start = pos;
        std::size_t content_start = pos + opener.size();
        std::size_t closer = find_closer(content_start, opener);
        if (closer == npos) {
            // Incomplete: opener + rest of the source on this construct. We
            // consume the opener and treat the remainder as a single Incomplete
            // node (its own children parsed recursively is not needed for
            // losslessness; the range covers everything verbatim).
            flush_text_to(out, run_start, start);
            pos = content_start;
            std::size_t end = find_line_or_doc_end(pos);
            if (end > pos) pos = end;
            InlineCstNode node;
            node.id = next_id();
            node.kind = InlineCstKind::Incomplete;
            node.status = ParseStatus::MissingCloser;
            node.range = {start, pos};
            node.delim = {{start, pos}, {start, content_start}, {content_start, pos}, std::nullopt};
            out.push_back(std::move(node));
            run_start = pos;
            return true;
        }
        flush_text_to(out, run_start, start);
        std::size_t content_end = closer;
        std::size_t close_end = closer + opener.size();
        // Recurse into content for nested structure.
        Parser inner{src, ctx};
        inner.counter = counter;
        inner.pos = content_start;
        InlineCstNodes children = inner.parse_until(content_end);
        counter = inner.counter;
        pos = close_end;
        InlineCstNode node;
        node.id = next_id();
        node.kind = kind;
        node.status = ParseStatus::Complete;
        node.range = {start, pos};
        node.delim = {{start, pos}, {start, content_start}, {content_start, content_end}, SourceRange{content_end, close_end}};
        node.children = std::move(children);
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    bool starts_with(std::size_t at, std::u32string_view lit) const {
        if (at + lit.size() > limit) return false;
        for (std::size_t i = 0; i < lit.size(); ++i) if (src[at + i] != lit[i]) return false;
        return true;
    }

    // Find a matching closer for `opener` starting at `from`, not crossing a
    // blank line (double newline) — emphasis cannot span a paragraph break.
    std::size_t find_closer(std::size_t from, std::u32string_view opener) {
        for (std::size_t i = from; i + opener.size() <= limit; ++i) {
            if (src[i] == U'\n' && i + 1 < limit && src[i + 1] == U'\n') return npos;
            if (starts_with(i, opener)) {
                // right-flanking: char before closer must not be whitespace.
                if (i == 0) return i;
                char32_t prev = ch(i - 1);
                if (prev == U' ' || prev == U'\n' || prev == U'\t') continue;
                if (opener == U"_" && i + opener.size() < limit && ch(i + opener.size()) == U'_') continue;
                return i;
            }
        }
        return npos;
    }

    std::size_t find_line_or_doc_end(std::size_t from) {
        // For incomplete constructs, consume to end of this line (incl. newline)
        // or end of source.
        std::size_t i = from;
        while (i < limit && src[i] != U'\n') ++i;
        if (i < limit) ++i; // include the newline
        return i;
    }

    // ---- inline code `...` ----
    bool parse_code_span(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() != U'`') return false;
        if (pos > 0 && ch(pos - 1) == U'`') return false; // part of a longer run
        std::size_t start = pos;
        std::size_t count = 0;
        while (pos < limit && peek() == U'`') { ++count; advance(); }
        std::size_t content_start = pos;
        // find a closing run of exactly `count` backticks, not crossing blank line
        std::size_t search = pos;
        std::size_t closing = npos;
        while (search < limit) {
            if (src[search] == U'\n' && search + 1 < limit && src[search + 1] == U'\n') break;
            if (src[search] == U'`') {
                std::size_t run = search;
                while (search < limit && src[search] == U'`') ++search;
                if (search - run == count) { closing = run; break; }
            } else {
                ++search;
            }
        }
        if (closing == npos) {
            // Incomplete code span: keep opener + rest verbatim.
            flush_text_to(out, run_start, start);
            pos = find_line_or_doc_end(pos);
            InlineCstNode node;
            node.id = next_id();
            node.kind = InlineCstKind::Incomplete;
            node.status = ParseStatus::MissingCloser;
            node.range = {start, pos};
            node.delim = {{start, pos}, {start, content_start}, {content_start, pos}, std::nullopt};
            out.push_back(std::move(node));
            run_start = pos;
            return true;
        }
        flush_text_to(out, run_start, start);
        std::size_t content_end = closing;
        std::size_t close_end = closing + count;
        pos = close_end;
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::CodeSpan;
        node.status = ParseStatus::Complete;
        node.range = {start, pos};
        node.delim = {{start, pos}, {start, content_start}, {content_start, content_end}, SourceRange{content_end, close_end}};
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    // ---- inline math $...$ or \(...\) ----
    bool parse_math(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() == U'$' && ctx.dialect.math.inline_dollar) {
            if (peek(1) == U'$') return false; // $$ belongs to block path
            std::size_t start = pos;
            advance();
            std::size_t content_start = pos;
            while (!eof() && peek() != U'$' && peek() != U'\n') advance();
            if (peek() != U'$') {
                flush_text_to(out, run_start, start);
                pos = find_line_or_doc_end(pos);
                InlineCstNode node;
                node.id = next_id();
                node.kind = InlineCstKind::Incomplete;
                node.status = ParseStatus::MissingCloser;
                node.range = {start, pos};
                node.delim = {{start, pos}, {start, content_start}, {content_start, pos}, std::nullopt};
                out.push_back(std::move(node));
                run_start = pos;
                return true;
            }
            flush_text_to(out, run_start, start);
            std::size_t content_end = pos;
            advance();
            std::size_t close_end = pos;
            InlineCstNode node;
            node.id = next_id();
            node.kind = InlineCstKind::InlineMath;
            node.status = ParseStatus::Complete;
            node.range = {start, pos};
            node.delim = {{start, pos}, {start, content_start}, {content_start, content_end}, SourceRange{content_end, close_end}};
            node.math_delim = MathDelimiter::InlineDollar;
            out.push_back(std::move(node));
            run_start = pos;
            return true;
        }
        if (peek() == U'\\' && peek(1) == U'(' && ctx.dialect.math.inline_paren) {
            std::size_t start = pos;
            advance(2);
            std::size_t content_start = pos;
            while (!eof() && !(peek() == U'\\' && peek(1) == U')') && peek() != U'\n') advance();
            if (!(peek() == U'\\' && peek(1) == U')')) {
                flush_text_to(out, run_start, start);
                pos = find_line_or_doc_end(pos);
                InlineCstNode node;
                node.id = next_id();
                node.kind = InlineCstKind::Incomplete;
                node.status = ParseStatus::MissingCloser;
                node.range = {start, pos};
                node.delim = {{start, pos}, {start, content_start}, {content_start, pos}, std::nullopt};
                out.push_back(std::move(node));
                run_start = pos;
                return true;
            }
            flush_text_to(out, run_start, start);
            std::size_t content_end = pos;
            advance(2);
            std::size_t close_end = pos;
            InlineCstNode node;
            node.id = next_id();
            node.kind = InlineCstKind::InlineMath;
            node.status = ParseStatus::Complete;
            node.range = {start, pos};
            node.delim = {{start, pos}, {start, content_start}, {content_start, content_end}, SourceRange{content_end, close_end}};
            node.math_delim = MathDelimiter::InlineParen;
            out.push_back(std::move(node));
            run_start = pos;
            return true;
        }
        return false;
    }

    // ---- link / image [text](url) / ![alt](url) / [text][ref] ----
    bool parse_link_or_image(InlineCstNodes& out, std::size_t& run_start) {
        std::size_t start = pos;
        bool is_image = false;
        if (peek() == U'!' && peek(1) == U'[') { is_image = true; advance(); }
        if (peek() != U'[') { pos = start; return false; }
        advance();
        std::size_t text_start = pos;
        std::size_t depth = 1;
        while (!eof() && depth > 0 && peek() != U'\n') {
            if (peek() == U'\\' && pos + 1 < limit) { advance(2); continue; }
            if (peek() == U'[') ++depth;
            else if (peek() == U']') --depth;
            if (depth > 0) advance();
        }
        if (depth != 0 || peek() != U']') {
            // Incomplete: opener `[` (and `!`) + rest verbatim.
            pos = start;
            return parse_incomplete_bracket(out, run_start, start);
        }
        std::size_t text_end = pos;
        advance(); // consume ']'
        // Inline destination `( ... )`
        if (peek() == U'(') {
            advance();
            while (!eof() && (peek() == U' ' || peek() == U'\t')) advance();
            std::u32string href;
            std::optional<std::string> title;
            bool angle = false;
            if (peek() == U'<') { angle = true; advance(); while (!eof() && peek() != U'>' && peek() != U'\n') href.push_back(peek()), advance(); if (peek() != U'>') { pos = start; return parse_incomplete_bracket(out, run_start, start); } advance(); }
            else {
                std::size_t parens = 0;
                while (!eof() && peek() != U'\n') {
                    if (peek() == U'\\' && pos + 1 < limit) { href.push_back(peek(1)); advance(2); continue; }
                    if (peek() == U'(') { ++parens; href.push_back(peek()); advance(); continue; }
                    if (peek() == U')') { if (parens == 0) break; --parens; href.push_back(peek()); advance(); continue; }
                    if ((peek() == U' ' || peek() == U'\t') && parens == 0) break;
                    href.push_back(peek()); advance();
                }
            }
            (void)angle;
            while (!eof() && (peek() == U' ' || peek() == U'\t')) advance();
            if (!eof() && (peek() == U'"' || peek() == U'\'' || peek() == U'(')) {
                char32_t opening = peek();
                char32_t closing = opening == U'(' ? U')' : opening;
                advance();
                std::u32string value;
                while (!eof() && peek() != closing && peek() != U'\n') value.push_back(peek()), advance();
                if (peek() != closing) { pos = start; return parse_incomplete_bracket(out, run_start, start); }
                advance();
                while (!eof() && (peek() == U' ' || peek() == U'\t')) advance();
                title = cps_to_utf8(value);
            }
            if (peek() != U')') { pos = start; return parse_incomplete_bracket(out, run_start, start); }
            advance();
            std::size_t source_end = pos;
            flush_text_to(out, run_start, start);
            // Recurse into text content for links (images keep alt as plain text).
            InlineCstNodes children;
            if (!is_image) {
                Parser inner{src, ctx};
                inner.counter = counter;
                inner.pos = text_start;
                children = inner.parse_until(text_end);
                counter = inner.counter;
            }
            InlineCstNode node;
            node.id = next_id();
            node.kind = is_image ? InlineCstKind::Image : InlineCstKind::Link;
            node.status = ParseStatus::Complete;
            node.range = {start, source_end};
            node.delim = {{start, source_end}, {start, text_start}, {text_start, text_end}, SourceRange{text_end, source_end}};
            node.href = cps_to_utf8(href);
            node.title = std::move(title);
            if (is_image) node.alt = cps_to_utf8(src.substr(text_start, text_end - text_start));
            node.children = std::move(children);
            out.push_back(std::move(node));
            pos = source_end;
            run_start = pos;
            return true;
        }
        // Reference link [text][ref] or [text][]
        if (peek() == U'[') {
            advance();
            std::size_t label_start = pos;
            while (!eof() && peek() != U']' && peek() != U'\n') advance();
            if (peek() != U']') { pos = start; return parse_incomplete_bracket(out, run_start, start); }
            std::size_t label_end = pos;
            advance();
            std::string label = label_end == label_start
                ? normalize_label(src.substr(text_start, text_end - text_start))
                : normalize_label(src.substr(label_start, label_end - label_start));
            std::optional<InlineLinkDef> def;
            if (ctx.resolve_link_label) def = ctx.resolve_link_label(label);
            if (!def) { pos = start; return parse_incomplete_bracket(out, run_start, start); }
            std::size_t source_end = pos;
            flush_text_to(out, run_start, start);
            InlineCstNodes children;
            if (!is_image) {
                Parser inner{src, ctx};
                inner.counter = counter;
                inner.pos = text_start;
                children = inner.parse_until(text_end);
                counter = inner.counter;
            }
            InlineCstNode node;
            node.id = next_id();
            node.kind = is_image ? InlineCstKind::Image : InlineCstKind::Link;
            node.status = ParseStatus::Complete;
            node.range = {start, source_end};
            node.delim = {{start, source_end}, {start, text_start}, {text_start, text_end}, SourceRange{text_end, source_end}};
            node.href = def->href;
            node.title = def->title;
            if (is_image) node.alt = cps_to_utf8(src.substr(text_start, text_end - text_start));
            node.children = std::move(children);
            out.push_back(std::move(node));
            pos = source_end;
            run_start = pos;
            return true;
        }
        // Just `[text]` with no destination — treat the `[` opener as incomplete
        // (it may become a link as the user keeps typing).
        pos = start;
        return parse_incomplete_bracket(out, run_start, start);
    }

    // Emit an Incomplete node for a bracket opener that didn't close into a
    // link/image. Consumes the `[` (and `!` if image) and the rest of the line.
    bool parse_incomplete_bracket(InlineCstNodes& out, std::size_t& run_start, std::size_t start) {
        flush_text_to(out, run_start, start);
        pos = start;
        std::size_t opener_end = start + 1;
        if (ch(start) == U'!') opener_end = start + 2;
        // Consume to end of line (don't swallow following lines).
        std::size_t end = start;
        while (end < limit && src[end] != U'\n') ++end;
        pos = end;
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::Incomplete;
        node.status = ParseStatus::MissingCloser;
        node.range = {start, pos};
        node.delim = {{start, pos}, {start, opener_end}, {opener_end, pos}, std::nullopt};
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    static std::string normalize_label(std::u32string_view label) {
        std::string out;
        for (char32_t c : label) {
            if (c == U' ' || c == U'\t' || c == U'\n') { if (!out.empty() && out.back() != ' ') out.push_back(' '); }
            else out.push_back(static_cast<char>(c >= 0x80 ? '?' : std::tolower(static_cast<unsigned char>(c))));
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    // ---- autolink <url> / <email> ----
    bool parse_autolink(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() != U'<') return false;
        std::size_t start = pos;
        advance();
        std::size_t content_start = pos;
        std::u32string value;
        while (!eof() && peek() != U'>' && peek() != U'\n' && peek() != U' ' && peek() != U'\t') value.push_back(peek()), advance();
        if (peek() != U'>' || value.empty()) { pos = start; return false; }
        auto text = cps_to_utf8(value);
        auto lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool url = lower.starts_with("http://") || lower.starts_with("https://");
        auto at = text.find('@');
        bool email = at != std::string::npos && at > 0 && at + 1 < text.size() && text.find('@', at + 1) == std::string::npos;
        if (!url && !email) { pos = start; return false; }
        advance();
        flush_text_to(out, run_start, start);
        std::size_t close_end = pos;
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::Autolink;
        node.status = ParseStatus::Complete;
        node.range = {start, pos};
        node.delim = {{start, close_end}, {start, content_start}, {content_start, close_end - 1}, SourceRange{close_end - 1, close_end}};
        node.href = email ? "mailto:" + text : text;
        node.children.push_back(InlineCstNode{next_id(), InlineCstKind::Text, {content_start, close_end - 1}, ParseStatus::Complete});
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    // ---- footnote ref [^label] ----
    bool parse_footnote_ref(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() != U'[' || peek(1) != U'^') return false;
        std::size_t start = pos;
        advance(2);
        std::size_t label_start = pos;
        while (!eof() && peek() != U']' && peek() != U'\n') advance();
        if (peek() != U']') { pos = start; return parse_incomplete_bracket(out, run_start, start); }
        std::size_t label_end = pos;
        advance();
        flush_text_to(out, run_start, start);
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::FootnoteRef;
        node.status = ParseStatus::Complete;
        node.range = {start, pos};
        node.delim = {{start, pos}, {start, label_start}, {label_start, label_end}, SourceRange{label_end, pos}};
        node.label = cps_to_utf8(src.substr(label_start, label_end - label_start));
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    // ---- wiki link [[target|alias]] / [[target]] ----
    bool parse_wiki_link(InlineCstNodes& out, std::size_t& run_start) {
        if (peek() != U'[' || peek(1) != U'[') return false;
        std::size_t start = pos;
        advance(2);
        std::size_t content_start = pos;
        std::u32string body;
        while (!eof() && !(peek() == U']' && peek(1) == U']') && peek() != U'\n') body.push_back(peek()), advance();
        if (!(peek() == U']' && peek(1) == U']')) { pos = start; return parse_incomplete_bracket(out, run_start, start); }
        std::size_t content_end = pos;
        advance(2);
        flush_text_to(out, run_start, start);
        std::string target;
        std::optional<std::string> alias;
        auto bar = body.find(U'|');
        if (bar != std::u32string::npos) {
            target = cps_to_utf8(body.substr(0, bar));
            alias = cps_to_utf8(body.substr(bar + 1));
        } else {
            target = cps_to_utf8(body);
        }
        InlineCstNode node;
        node.id = next_id();
        node.kind = InlineCstKind::WikiLink;
        node.status = ParseStatus::Complete;
        node.range = {start, pos};
        node.delim = {{start, pos}, {start, content_start}, {content_start, content_end}, SourceRange{content_end, pos}};
        node.target = std::move(target);
        node.alias = std::move(alias);
        out.push_back(std::move(node));
        run_start = pos;
        return true;
    }

    static bool delimiter_char(char32_t c) {
        switch (c) {
            case U'*': case U'_': case U'~': case U'`': case U'$':
            case U'[': case U']': case U'(': case U')': case U'!':
            case U'<': case U'>': case U'"': case U'\'':
                return true;
            default:
                return false;
        }
    }

    // Produce the lexical leaf stream independently of the structural parse.
    // Ranges are exhaustive and never overlap, so concatenating token source
    // slices is a real leaf-token flatten, not a top-level node shortcut.
    std::vector<InlineToken> tokenize() {
        std::vector<InlineToken> tokens;
        std::size_t cursor = 0;
        auto emit = [&](TokenKind kind, std::size_t start, std::size_t end) {
            tokens.push_back(InlineToken{next_id(), kind, {start, end}});
        };

        while (cursor < limit) {
            const auto start = cursor;
            const auto c = src[cursor];

            if (c == U'\n') {
                const bool hard = (cursor >= 2 && src[cursor - 1] == U' ' && src[cursor - 2] == U' ')
                    || (cursor >= 1 && src[cursor - 1] == U'\\');
                ++cursor;
                emit(hard ? TokenKind::HardBreak : TokenKind::SoftBreak, start, cursor);
                continue;
            }
            if (c == U' ' || c == U'\t') {
                while (cursor < limit && (src[cursor] == U' ' || src[cursor] == U'\t')) ++cursor;
                emit(TokenKind::Whitespace, start, cursor);
                continue;
            }
            if (c == U'\\' && cursor + 1 < limit && ascii_punct(src[cursor + 1])) {
                cursor += 2;
                emit(TokenKind::Escape, start, cursor);
                continue;
            }
            if (c == U'&') {
                const auto end = entity_end(src.substr(0, limit), cursor);
                if (end != npos) {
                    cursor = end;
                    emit(TokenKind::Entity, start, cursor);
                    continue;
                }
            }
            if (delimiter_char(c)) {
                while (cursor < limit && delimiter_char(src[cursor])) ++cursor;
                emit(TokenKind::Delimiter, start, cursor);
                continue;
            }

            ++cursor;
            while (cursor < limit) {
                const auto next = src[cursor];
                if (next == U'\n' || next == U' ' || next == U'\t' || delimiter_char(next)) break;
                if (next == U'\\' && cursor + 1 < limit && ascii_punct(src[cursor + 1])) break;
                if (next == U'&' && entity_end(src.substr(0, limit), cursor) != npos) break;
                ++cursor;
            }
            emit(TokenKind::Text, start, cursor);
        }
        return tokens;
    }

    // Parse the full source into a structural tree and an exhaustive token
    // stream over the same source.
    InlineCstTree parse_all() {
        InlineCstNodes out;
        std::size_t run_start = 0;
        while (!eof()) {
            if (parse_break(out, run_start)) continue;
            if (peek() == U'\n') { advance(); continue; } // stray newline safety
            if (parse_escape(out, run_start)) continue;
            if (parse_entity(out, run_start)) continue;
            // math before delimited so `$` / `\(` aren't eaten as text
            if (peek() == U'$' || (peek() == U'\\' && peek(1) == U'(')) {
                if (parse_math(out, run_start)) continue;
            }
            if (peek() == U'*' && peek(1) == U'*') { if (parse_delimited(out, run_start, U"**", InlineCstKind::Strong)) continue; }
            if (peek() == U'*') { if (parse_delimited(out, run_start, U"*", InlineCstKind::Emphasis)) continue; }
            if (peek() == U'_' && peek(1) == U'_') { if (parse_delimited(out, run_start, U"__", InlineCstKind::Strong)) continue; }
            if (peek() == U'_') { if (parse_delimited(out, run_start, U"_", InlineCstKind::Emphasis)) continue; }
            if (peek() == U'~' && peek(1) == U'~') { if (parse_delimited(out, run_start, U"~~", InlineCstKind::Strikethrough)) continue; }
            if (peek() == U'`') { if (parse_code_span(out, run_start)) continue; }
            if (peek() == U'!' && peek(1) == U'[') { if (parse_link_or_image(out, run_start)) continue; }
            if (peek() == U'[') {
                if (peek(1) == U'^') { if (parse_footnote_ref(out, run_start)) continue; }
                if (peek(1) == U'[') { if (parse_wiki_link(out, run_start)) continue; }
                if (parse_link_or_image(out, run_start)) continue;
            }
            if (peek() == U'<') { if (parse_autolink(out, run_start)) continue; }
            // ordinary character — accumulate into the text run.
            advance();
        }
        flush_text(out, run_start);
        return InlineCstTree{std::move(out), tokenize()};
    }

    // Parse [from, limit) — used for nested content.
    InlineCstNodes parse_until(std::size_t requested_limit) {
        limit = (std::min)(limit, requested_limit);
        InlineCstNodes out;
        std::size_t run_start = pos;
        while (!eof()) {
            if (peek() == U'\n') {
                // soft break within nested content (no hard-break detection
                // needed for losslessness; the newline becomes its own node).
                flush_text(out, run_start);
                std::size_t nl = pos;
                advance();
                InlineCstNode sb;
                sb.id = next_id();
                sb.kind = InlineCstKind::SoftBreak;
                sb.range = {nl, pos};
                sb.delim = {{nl, pos}, {nl, pos}, {nl, pos}, std::nullopt};
                out.push_back(std::move(sb));
                run_start = pos;
                continue;
            }
            if (parse_escape(out, run_start)) continue;
            if (parse_entity(out, run_start)) continue;
            if (peek() == U'$' || (peek() == U'\\' && peek(1) == U'(')) { if (parse_math(out, run_start)) continue; }
            if (peek() == U'*' && peek(1) == U'*') { if (parse_delimited(out, run_start, U"**", InlineCstKind::Strong)) continue; }
            if (peek() == U'*') { if (parse_delimited(out, run_start, U"*", InlineCstKind::Emphasis)) continue; }
            if (peek() == U'_' && peek(1) == U'_') { if (parse_delimited(out, run_start, U"__", InlineCstKind::Strong)) continue; }
            if (peek() == U'_') { if (parse_delimited(out, run_start, U"_", InlineCstKind::Emphasis)) continue; }
            if (peek() == U'~' && peek(1) == U'~') { if (parse_delimited(out, run_start, U"~~", InlineCstKind::Strikethrough)) continue; }
            if (peek() == U'`') { if (parse_code_span(out, run_start)) continue; }
            if (peek() == U'!' && peek(1) == U'[') { if (parse_link_or_image(out, run_start)) continue; }
            if (peek() == U'[') {
                if (peek(1) == U'^') { if (parse_footnote_ref(out, run_start)) continue; }
                if (peek(1) == U'[') { if (parse_wiki_link(out, run_start)) continue; }
                if (parse_link_or_image(out, run_start)) continue;
            }
            if (peek() == U'<') { if (parse_autolink(out, run_start)) continue; }
            advance();
        }
        flush_text(out, run_start);
        return out;
    }
};

} // namespace inline_parser_detail

inline InlineCstTree parse_inline(std::u32string_view source, const InlineParseContext& ctx) {
    inline_parser_detail::Parser parser{source, ctx};
    return parser.parse_all();
}

inline void reparse_inline_document(InlineDocument& document, const InlineParseContext& ctx) {
    document.tree = parse_inline(document.source, ctx);
}

} // namespace elmd

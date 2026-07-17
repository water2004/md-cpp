// elmd.core.utf — UTF-16 / grapheme helpers over std::u32string / std::string.
// We model char offsets as Unicode scalar values (Rust char); great care taken
// to never mix with byte/utf16 offsets.
export module elmd.core.utf;
import std;

export namespace elmd {

// ---------------------------------------------------------------------------
// UTF-16 helpers (work on char32 codepoint stream).
// ---------------------------------------------------------------------------
inline std::size_t utf16_len_of_cp(char32_t c) {
    return c <= 0xFFFF ? 1 : 2;
}

// Sum len_utf16 over a string as char32 cps.
inline std::size_t utf16_len(std::u32string_view cps) {
    std::size_t n = 0;
    for (char32_t c : cps) n += utf16_len_of_cp(c);
    return n;
}

// char index -> utf16 index, given the cps.
inline std::size_t char_index_to_utf16(std::u32string_view cps, std::size_t char_index) {
    std::size_t pos = 0;
    for (std::size_t i = 0; i < char_index && i < cps.size(); ++i) pos += utf16_len_of_cp(cps[i]);
    return pos;
}

// utf16 index -> char index
inline std::size_t utf16_to_char_index(std::u32string_view cps, std::size_t utf16_index) {
    std::size_t pos = 0, i = 0;
    while (i < cps.size() && pos < utf16_index) { pos += utf16_len_of_cp(cps[i]); ++i; }
    return i;
}

// svps: cps of text. Returns (start_char, end_char) for the given utf16 bounds.
inline std::pair<std::size_t, std::size_t> utf16_slice_bounds(std::u32string_view cps, std::size_t s, std::size_t e) {
    return {utf16_to_char_index(cps, s), utf16_to_char_index(cps, e)};
}

// ---------------------------------------------------------------------------
// Grapheme cluster helpers — a lightweight cluster approximator.
// We approximate grapheme cluster boundaries using:
//   * CR+LF / other line breaks form a single cluster
//   * Extend/variation-selector codepoints attach to the previous base cp
//   * Regional indicator pairs (flag emoji) form a single cluster
//   * ZWJ concatenates adjacent clusters
// This is not the full UAX#29 algorithm but covers the spec's test cases
// (CJK, ASCII, BMP, surrogate pairs via cps, emoji ZWJ family).
// ---------------------------------------------------------------------------

inline bool is_cp_white(std::uint32_t c) {
    // ASCII whitespace + NBSP? Match Rust char::is_whitespace sufficient set.
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x00A0 || c == 0x2028 || c == 0x2029;
}

// Classification helpers used by grapheme iteration.
inline bool is_regional_indicator(char32_t c) { return c >= 0x1F1E6 && c <= 0x1F1FF; }
inline bool is_extend_class(char32_t c) {
    // U+0300–U+036F combining diacritics + variation selectors + emoji modifiers.
    return (c >= 0x0300 && c <= 0x036F) || c == 0x200D /* ZWJ */
        || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFE0F
        || (c >= 0x1F3FB && c <= 0x1F3FF) /* skin-tone */
        || (c >= 0xE0020 && c <= 0xE007F);
}

// count_graphemes over a cps view.
inline std::size_t count_graphemes(std::u32string_view cps) {
    if (cps.empty()) return 0;
    std::size_t n = 1;
    bool last_was_ri = false;
    for (std::size_t i = 1; i < cps.size(); ++i) {
        char32_t prev = cps[i - 1];
        char32_t cur = cps[i];
        // CR+LF
        if (prev == '\r' && cur == '\n') { continue; }
        // Extend attaches
        if (is_extend_class(cur)) { continue; }
        // ZWJ already produces attach through is_extend_class.
        // Regional indicator second of a pair
        if (is_regional_indicator(prev) && is_regional_indicator(cur) && last_was_ri) {
            continue; // pair collapsed
        }
        last_was_ri = is_regional_indicator(cur);
        ++n;
    }
    return n;
}

// Find char index of the start of the grapheme cluster whose content *precedes*
// `char_index`. Returns 0 if at the very start.
inline std::size_t prev_grapheme_boundary_char(std::u32string_view cps, std::size_t char_index) {
    if (char_index == 0 || cps.empty()) return 0;
    if (char_index > cps.size()) char_index = cps.size();
    // Walk backwards until we find a non-extend, non (RI continuation), non ZWJ predecessor.
    std::size_t i = char_index;
    while (i > 0) {
        char32_t cur = cps[i - 1];
        --i;
        if (i == 0) { return 0; }
        char32_t before = cps[i - 1];
        if (is_extend_class(cur)) continue;             // cur is part of cluster with `before`
        if (cur == '\n' && before == '\r') continue;     // CR+LF cluster with further base
        if (is_regional_indicator(cur) && is_regional_indicator(before)) continue;
        // cur is a cluster base; the boundary is at i.
        break;
    }
    return i;
}

// Find char index just after the grapheme cluster that begins at or contains
// `char_index`. Pair with prev for caret stepping semantics.
inline std::size_t next_grapheme_boundary_char(std::u32string_view cps, std::size_t char_index) {
    if (char_index >= cps.size() || cps.empty()) return cps.size();
    std::size_t i = char_index;
    // If we land on an extend cp, advance to a base first.
    while (i < cps.size() && is_extend_class(cps[i])) ++i;
    if (i >= cps.size()) return cps.size();
    // base at i, advance one.
    const auto base = cps[i];
    ++i;
    // Unicode grapheme rule GB3: CR × LF. A physical CRLF terminator is one
    // caret/delete unit, matching the reverse-boundary implementation above.
    if (base == U'\r' && i < cps.size() && cps[i] == U'\n') ++i;
    // consume trailing extends / ZWJ clusters.
    while (i < cps.size()) {
        char32_t cur = cps[i];
        if (is_extend_class(cur)) { ++i; continue; }
        // Regional indicator pair after an RI base
        if (is_regional_indicator(cps[i - 1]) && is_regional_indicator(cur)) { ++i; continue; }
        break;
    }
    return i;
}

// ---------------------------------------------------------------------------
// Convenience converters between UTF-8 (std::string) and cps (std::u32string).
// ---------------------------------------------------------------------------
inline std::u32string utf8_to_cps(std::string_view utf8) {
    std::u32string out;
    std::size_t i = 0;
    while (i < utf8.size()) {
        char32_t out_cp = 0; int len = 0;
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        if ((c & 0x80) == 0) { out_cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { out_cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { out_cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { out_cp = c & 0x07; len = 4; }
        else { out_cp = c; len = 1; }
        for (int k = 1; k < len && i + k < utf8.size(); ++k)
            out_cp = (out_cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        out.push_back(out_cp);
        i += len;
    }
    return out;
}

inline std::string cps_to_utf8(std::u32string_view cps) {
    std::string out;
    auto push = [&](char32_t c) {
        if (c <= 0x7F) { out.push_back(static_cast<char>(c)); }
        else if (c <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    };
    for (char32_t c : cps) push(c);
    return out;
}

} // namespace elmd

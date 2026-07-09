// elmd.core.buffer — TextBuffer: char-indexed storage with line index and
// offset conversions. A full rope is not justified at v1; we keep a contiguous
// UTF-32 char vector plus a byte index for byte conversions, preserving the
// public API and invariants (revision monotonically increments; edits applied
// in reverse order in apply_delta).
export module elmd.core.buffer;
import std;
import elmd.core.types;
import elmd.core.utf;

export namespace elmd {

// A TextEdit as accepted by the buffer (edit-only; `original` lives in the
// editor's transaction layer).
struct BufferTextEdit {
    TextRange<CharOffset> range;
    std::u32string replacement; // in cps
};

struct TextDelta {
    std::uint64_t revision_before = 0;
    std::vector<BufferTextEdit> edits;
};

// Stores text as Unicode code points (char32). `byte_offsets[cp_index+1]` holds
// the UTF-8 byte offset of code point [cp_index]; byte_offsets[0]=0.
class TextBuffer {
public:
    TextBuffer() : revision_(1) { byte_offsets_.push_back(0); }

    static TextBuffer from_text(const std::string& utf8) {
        TextBuffer b;
        b.set_text_(utf8);
        return b;
    }
    static TextBuffer from_cps(std::u32string cps) {
        TextBuffer b;
        b.cps_ = std::move(cps);
        b.rebuild_byte_offsets_();
        return b;
    }

    std::uint64_t revision() const { return revision_; }
    std::size_t len_chars() const { return cps_.size(); }
    std::size_t len_bytes() const { return byte_offsets_.empty() ? 0 : byte_offsets_.back(); }
    bool is_empty() const { return cps_.empty(); }

    std::u32string_view text_cps() const { return cps_; }

    std::string text_utf8() const {
        std::string out;
        out.reserve(byte_offsets_.empty() ? 0 : byte_offsets_.back());
        for (char32_t c : cps_) append_utf8_(out, c);
        return out;
    }

    // text of a [start,end) Char range, clamped to buffer.
    std::u32string text_range(TextRange<CharOffset> r) const {
        std::size_t s = std::min(r.start.v, cps_.size());
        std::size_t e = std::min(std::max(r.end.v, s), cps_.size());
        if (s >= e) return {};
        return cps_.substr(s, e - s);
    }

    // ---- offset conversions ----
    std::size_t char_to_byte(CharOffset c) const {
        if (c.v >= cps_.size()) return byte_offsets_.empty() ? 0 : byte_offsets_.back();
        return byte_offsets_[c.v];
    }
    // Given utf8 string, byte index -> char index.
    CharOffset byte_to_char(std::size_t byte_dix) const {
        // the start byte of each cp.
        auto it = std::upper_bound(byte_offsets_.begin(), byte_offsets_.end(), byte_dix);
        std::size_t idx = (it == byte_offsets_.begin()) ? 0 : static_cast<std::size_t>(it - byte_offsets_.begin()) - 1;
        if (idx > cps_.size()) idx = cps_.size();
        return CharOffset(idx);
    }
    Utf16Offset char_to_utf16(CharOffset c) const {
        return Utf16Offset(char_index_to_utf16(cps_, std::min(c.v, cps_.size())));
    }
    CharOffset utf16_to_char(Utf16Offset u) const {
        return CharOffset(utf16_to_char_index(cps_, u.v));
    }
    GraphemeOffset char_to_grapheme(CharOffset c) const {
        return GraphemeOffset(elmd::count_graphemes(cps_.substr(0, std::min(c.v, cps_.size()))));
    }

    // ---- line index ----
    std::size_t line_of_char(CharOffset c) const {
        std::size_t pos = std::min(c.v, cps_.size());
        std::size_t line = 0;
        for (std::size_t i = 0; i < pos; ++i) if (cps_[i] == '\n') ++line;
        return line;
    }
    CharOffset char_of_line(std::size_t line) const {
        if (cps_.empty()) return CharOffset(0);
        std::size_t cur = 0, l = 0;
        while (l < line && cur < cps_.size()) { if (cps_[cur] == '\n') ++l; ++cur; }
        return CharOffset(cur);
    }
    std::size_t len_lines() const {
        if (cps_.empty()) return 1;
        std::size_t n = 1;
        for (char32_t c : cps_) if (c == '\n') ++n;
        return n;
    }
    TextRange<CharOffset> line_range(std::size_t line) const {
        CharOffset start = char_of_line(line);
        std::size_t s = start.v, e = s;
        while (e < cps_.size() && cps_[e] != '\n') ++e;
        return {start, CharOffset(e)};
    }

    LineCol line_col_of_char(CharOffset c) const {
        std::size_t pos = std::min(c.v, cps_.size());
        std::size_t line = 0, line_start = 0;
        for (std::size_t i = 0; i < pos; ++i) {
            if (cps_[i] == '\n') { ++line; line_start = i + 1; }
        }
        return {line, pos - line_start};
    }
    CharOffset char_of_line_col(LineCol lc) const {
        CharOffset start = char_of_line(lc.line);
        auto lr = line_range(lc.line);
        std::size_t end = lr.end.v;
        std::size_t col = std::min(lc.column, end - lr.start.v);
        return CharOffset(lr.start.v + col);
    }

    // ---- mutation: apply edits in REVERSE order, bump revision ----
    void apply_delta(const TextDelta& delta) {
        for (auto it = delta.edits.rbegin(); it != delta.edits.rend(); ++it) {
            std::size_t s = std::min(it->range.start.v, cps_.size());
            std::size_t e = std::min(std::max(it->range.end.v, s), cps_.size());
            if (s < e) cps_.erase(cps_.begin() + s, cps_.begin() + e);
            if (!it->replacement.empty()) cps_.insert(cps_.begin() + s, it->replacement.begin(), it->replacement.end());
        }
        rebuild_byte_offsets_();
        revision_ += 1;
    }

    struct Snapshot {
        std::uint64_t revision;
        std::string text;
    };
    Snapshot snapshot() const { return {revision_, text_utf8()}; }

private:
    std::u32string cps_;
    std::vector<std::size_t> byte_offsets_;
    std::uint64_t revision_ = 1;

    void set_text_(const std::string& utf8) {
        cps_.clear();
        std::size_t i = 0;
        while (i < utf8.size()) {
            char32_t out = 0; int len = 0;
            char c = static_cast<unsigned char>(utf8[i]);
            if ((c & 0x80) == 0) { out = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { out = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { out = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { out = c & 0x07; len = 4; }
            else { out = c; len = 1; }
            for (int k = 1; k < len && i + k < utf8.size(); ++k) {
                out = (out << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
            }
            cps_.push_back(out);
            i += len;
        }
        rebuild_byte_offsets_();
        revision_ = 1;
    }

    void rebuild_byte_offsets_() {
        byte_offsets_.assign(1, 0);
        byte_offsets_.reserve(cps_.size() + 1);
        for (char32_t c : cps_) {
            std::size_t len;
            if (c <= 0x7F) len = 1;
            else if (c <= 0x7FF) len = 2;
            else if (c <= 0xFFFF) len = 3;
            else len = 4;
            byte_offsets_.push_back(byte_offsets_.back() + len);
        }
    }

    static void append_utf8_(std::string& out, char32_t c) {
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
    }
};

// delta.rs: map_offset_through (forward adjustment of an offset given a delta).
inline CharOffset map_offset_through_delta(CharOffset off, const TextDelta& delta) {
    std::size_t o = off.v;
    for (const auto& edit : delta.edits) {
        if (o >= edit.range.end.v) {
            o = o - (edit.range.end.v - edit.range.start.v) + edit.replacement.size();
        } else if (o >= edit.range.start.v) {
            o = edit.range.start.v;
        }
    }
    return CharOffset(o);
}

} // namespace elmd
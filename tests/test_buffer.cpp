import std;
#include "test_framework.h"
import elmd.core.types;
import elmd.core.ids;
import elmd.core.error;
import elmd.core.buffer;
import elmd.core.utf;
import elmd.core.transaction;

using namespace elmd;

ELMD_TEST(test_new_buffer) {
    TextBuffer b;
    ELMD_CHECK(b.is_empty());
    ELMD_CHECK_EQ(b.len_chars(), 0u);
    ELMD_CHECK_EQ(b.revision(), 1u);
}

ELMD_TEST(test_from_text) {
    TextBuffer b = TextBuffer::from_text("hello world");
    ELMD_CHECK_EQ(b.len_chars(), 11u);
    ELMD_CHECK_EQ(b.text_utf8(), std::string("hello world"));
}

ELMD_TEST(test_line_col) {
    TextBuffer b = TextBuffer::from_text("hello\nworld\n");
    LineCol lc = b.line_col_of_char(CharOffset(7));
    ELMD_CHECK_EQ(lc.line, 1u);
    ELMD_CHECK_EQ(lc.column, 1u);
}

ELMD_TEST(test_char_to_utf16_roundtrip) {
    TextBuffer b = TextBuffer::from_text("hello world");
    CharOffset c(5);
    auto u = b.char_to_utf16(c);
    ELMD_CHECK_EQ(u.v, 5u);
    ELMD_CHECK_EQ(b.utf16_to_char(u).v, 5u);
}

ELMD_TEST(test_char_to_byte_roundtrip) {
    TextBuffer b = TextBuffer::from_text("hello world");
    CharOffset c(5);
    auto by = b.char_to_byte(c);
    ELMD_CHECK_EQ(by, 5u);
    ELMD_CHECK_EQ(b.byte_to_char(by).v, 5u);
}

ELMD_TEST(test_apply_delta_insert) {
    TextBuffer b = TextBuffer::from_text("hello");
    TextDelta d; BufferTextEdit e;
    e.range = CharRange(CharOffset(5), CharOffset(5)); e.replacement = U" world";
    d.edits.push_back(e);
    b.apply_delta(d);
    ELMD_CHECK_EQ(b.text_utf8(), std::string("hello world"));
    ELMD_CHECK_EQ(b.revision(), 2u);
}

ELMD_TEST(test_apply_delta_delete) {
    TextBuffer b = TextBuffer::from_text("hello world");
    TextDelta d; BufferTextEdit e;
    e.range = CharRange(CharOffset(5), CharOffset(11)); e.replacement = U"";
    d.edits.push_back(e);
    b.apply_delta(d);
    ELMD_CHECK_EQ(b.text_utf8(), std::string("hello"));
}

ELMD_TEST(test_unicode_surrogate_pairs_utf16) {
    TextBuffer b = TextBuffer::from_text("hello \xF0\x9F\x8E\x89"); // "hello 🎉"
    auto u = b.char_to_utf16(CharOffset(7)); // The emoji at char idx 6
    (void)u;
    // rounding: char idx 7 -> utf16 idx 8 (emoji is two units after 6 ascii)... actually emoji at 6
    auto c = b.utf16_to_char(Utf16Offset(8));
    ELMD_CHECK(c.v >= 6);
}

ELMD_TEST(test_delta_map_offset_before_edit) {
    TextDelta d; BufferTextEdit e;
    e.range = CharRange(CharOffset(5), CharOffset(10)); e.replacement = U"rep";
    d.edits.push_back(e);
    ELMD_CHECK_EQ(map_offset_through_delta(CharOffset(0), d).v, 0u);
    ELMD_CHECK_EQ(map_offset_through_delta(CharOffset(3), d).v, 3u);
}

ELMD_TEST(test_delta_map_offset_after_edit) {
    TextDelta d; BufferTextEdit e;
    e.range = CharRange(CharOffset(5), CharOffset(10)); e.replacement = U"rep";
    d.edits.push_back(e);
    // offset 10 (was end) collapses to 5 + len(rep)=3 → 8
    ELMD_CHECK_EQ(map_offset_through_delta(CharOffset(10), d).v, 8u);
    ELMD_CHECK_EQ(map_offset_through_delta(CharOffset(12), d).v, 10u);
}

ELMD_TEST(test_grapheme_basic) {
    std::u32string s = U"hello";
    ELMD_CHECK_EQ(count_graphemes(s), 5u);
    ELMD_CHECK_EQ(prev_grapheme_boundary_char(s, 2), 1u);
}

ELMD_TEST(test_grapheme_emoji) {
    std::u32string s = U"a\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466b";
    auto nb = next_grapheme_boundary_char(s, 1);
    ELMD_CHECK(nb > 1); // emoji cluster is one grapheme, advance past it
}

ELMD_TEST(test_utf16_surrogate_slice) {
    std::u32string cps = U"a\U0001F389b"; // 3 cps, utf16_len 4
    ELMD_CHECK_EQ(utf16_len(cps), 4u);
    auto [s1, e1] = utf16_slice_bounds(cps, 1, 3);
    ELMD_CHECK_EQ(s1, 1u);
    (void)e1;
}

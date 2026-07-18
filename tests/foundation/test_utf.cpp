#include <cstddef>
#include <string>

#include "support/folia_test.hpp"

import elmd.core.utf;

using namespace boost::ut;
using namespace elmd;

suite utf_boundary_tests = [] {
    "utf16_offsets_are_explicit_at_platform_boundaries"_test = [] {
        const std::u32string text = U"a\U0001F600e\u0301";
        expect(utf16_len(text) == 5_u);
        expect(char_index_to_utf16(text, 0) == 0_u);
        expect(char_index_to_utf16(text, 1) == 1_u);
        expect(char_index_to_utf16(text, 2) == 3_u);
        expect(char_index_to_utf16(text, 4) == 5_u);
        expect(utf16_to_char_index(text, 1) == 1_u);
        expect(utf16_to_char_index(text, 3) == 2_u);
        expect(utf16_to_char_index(text, 5) == 4_u);
    };

    "utf16_offsets_never_split_combining_codepoints"_test = [] {
        const std::u32string text = U"x\u0301\U0001F469\u200D\U0001F4BB";
        for (std::size_t index = 0; index <= text.size(); ++index) {
            const auto utf16 = char_index_to_utf16(text, index);
            expect(utf16_to_char_index(text, utf16) == index);
        }
    };
};

// folia.core.block_line_recognizer — read-only Markdown block line recognition.
//
// This scanner classifies physical lines. It never allocates document nodes,
// advances parser state, or owns source coordinates.
export module folia.core.block_line_recognizer;
import std;
import folia.core.html_cst;
import folia.core.utf;

export namespace folia::detail {

class BlockLineRecognizer {
public:
    struct SetextUnderline {
        std::uint8_t level = 2;
        std::size_t line_end = 0;
    };

    explicit BlockLineRecognizer(std::u32string_view source) : source(source) {}

    static bool is_line_ending_character(char32_t value) {
        return value == U'\r' || value == U'\n';
    }

    char32_t ch_at(std::size_t at) const {
        return at < source.size() ? source[at] : 0;
    }

    std::size_t line_ending_length(std::size_t at) const {
        if (at >= source.size() || !is_line_ending_character(source[at])) return 0;
        return source[at] == U'\r' && at + 1 < source.size() && source[at + 1] == U'\n'
            ? 2u
            : 1u;
    }

    std::size_t line_content_end(std::size_t at) const {
        while (at < source.size() && !is_line_ending_character(source[at])) ++at;
        return at;
    }

    std::size_t next_line_start(std::size_t line_end) const {
        return line_end + line_ending_length(line_end);
    }

    bool is_line_start_at(std::size_t at) const {
        if (at == 0) return true;
        if (source[at - 1] == U'\n') return true;
        return source[at - 1] == U'\r'
            && (at >= source.size() || source[at] != U'\n');
    }

    bool line_starts_fenced_code(std::size_t at) const {
        std::size_t cursor = at;
        std::size_t indentation = 0;
        while (cursor < source.size() && source[cursor] == U' ' && indentation < 4) {
            ++cursor;
            ++indentation;
        }
        if (indentation > 3) return false;
        auto marker = ch_at(cursor);
        if (marker != U'`' && marker != U'~') return false;
        while (cursor < source.size() && source[cursor] == marker) ++cursor;
        return cursor - at - indentation >= 3;
    }

    bool line_is_thematic_break(std::size_t start, std::size_t* end = nullptr) const {
        const auto line_end = line_content_end(start);
        std::size_t cursor = start;
        std::size_t leading_spaces = 0;
        while (cursor < line_end && source[cursor] == U' ' && leading_spaces < 4) {
            ++cursor;
            ++leading_spaces;
        }
        if (leading_spaces > 3 || cursor >= line_end) return false;
        auto marker = source[cursor];
        if (marker != U'-' && marker != U'*' && marker != U'_') return false;
        std::size_t count = 0;
        while (cursor < line_end && source[cursor] == marker) { ++count; ++cursor; }
        while (cursor < line_end && (source[cursor] == U' ' || source[cursor] == U'\t')) ++cursor;
        if (cursor != line_end || count < 3) return false;
        if (end) *end = line_end;
        return true;
    }

    std::optional<SetextUnderline> setext_underline_at(std::size_t start) const {
        const auto line_end = line_content_end(start);
        std::size_t cursor = start;
        std::size_t leading_spaces = 0;
        while (cursor < line_end && source[cursor] == U' ' && leading_spaces < 4) {
            ++cursor;
            ++leading_spaces;
        }
        if (leading_spaces > 3 || cursor >= line_end) return std::nullopt;
        auto marker = source[cursor];
        if (marker != U'=' && marker != U'-') return std::nullopt;
        std::size_t count = 0;
        while (cursor < line_end && source[cursor] == marker) { ++cursor; ++count; }
        while (cursor < line_end && (source[cursor] == U' ' || source[cursor] == U'\t')) ++cursor;
        if (count == 0 || cursor != line_end) return std::nullopt;
        return SetextUnderline{static_cast<std::uint8_t>(marker == U'=' ? 1 : 2), line_end};
    }

    bool line_starts_block_html(std::size_t at) const {
        if (ch_at(at) != U'<') return false;
        auto cursor = at + 1;
        if (cursor >= source.size() || source[cursor] == U'/') return false;
        const auto name_start = cursor;
        while (cursor < source.size()
            && (is_ascii_alnum(source[cursor]) || source[cursor] == U'-')) {
            ++cursor;
        }
        if (cursor == name_start) return false;
        if (cursor < source.size()
            && source[cursor] != U' ' && source[cursor] != U'\t'
            && source[cursor] != U'\r' && source[cursor] != U'\n'
            && source[cursor] != U'>' && source[cursor] != U'/') {
            return false;
        }
        auto name = cps_to_utf8(source.substr(name_start, cursor - name_start));
        std::ranges::transform(name, name.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        return html_is_block_element(name);
    }

    bool line_starts_interrupting_block(std::size_t at) const {
        if (setext_underline_at(at)) return true;
        if (line_is_thematic_break(at)) return true;
        if (line_starts_block_html(at)) return true;
        if (ch_at(at) == U'>' || line_starts_fenced_code(at)) return true;
        if (ch_at(at) == U'#') {
            std::size_t cursor = at;
            while (cursor < source.size() && source[cursor] == U'#' && cursor - at < 6) ++cursor;
            if (cursor < source.size() && source[cursor] == U' ') return true;
        }
        if ((ch_at(at) == U'$' && ch_at(at + 1) == U'$')
            || (ch_at(at) == U'\\' && ch_at(at + 1) == U'[')) return true;
        if ((ch_at(at) == U'-' || ch_at(at) == U'+' || ch_at(at) == U'*')
            && ch_at(at + 1) == U' ') return true;
        if (is_ascii_digit(ch_at(at))) {
            std::size_t cursor = at;
            while (cursor < source.size() && is_ascii_digit(source[cursor])) ++cursor;
            if (cursor + 1 < source.size()
                && (source[cursor] == U'.' || source[cursor] == U')')
                && source[cursor + 1] == U' ') return true;
        }
        return false;
    }

private:
    static bool is_ascii_alnum(char32_t value) {
        return (value >= U'a' && value <= U'z')
            || (value >= U'A' && value <= U'Z')
            || is_ascii_digit(value);
    }

    static bool is_ascii_digit(char32_t value) {
        return value >= U'0' && value <= U'9';
    }

    std::u32string_view source;
};

}

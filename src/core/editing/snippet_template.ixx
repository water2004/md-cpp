// folia.core.snippet_template — source text, variables, and ordered tab stops.
export module folia.core.snippet_template;
import std;
import folia.core.text_edit;

export namespace folia {

struct SnippetTabStop {
    std::size_t index = 0;
    SourceRange range;

    bool operator==(SnippetTabStop const&) const = default;
};

struct ParsedSnippetTemplate {
    std::u32string text;
    std::vector<SnippetTabStop> tab_stops;
};

struct SnippetExpansionContext {
    std::optional<std::u32string_view> selected_text;
};

namespace snippet_detail {

struct IndexedStop {
    SnippetTabStop stop;
    std::size_t occurrence = 0;
};

inline bool decimal_digit(char32_t value) {
    return value >= U'0' && value <= U'9';
}

inline bool variable_start(char32_t value) {
    return (value >= U'A' && value <= U'Z') || value == U'_';
}

inline bool variable_continue(char32_t value) {
    return variable_start(value) || decimal_digit(value);
}

inline std::optional<std::size_t> parse_index(std::u32string_view source) {
    if (source.empty() || !std::ranges::all_of(source, decimal_digit))
        return std::nullopt;
    std::size_t index = 0;
    for (auto digit : source) {
        auto value = static_cast<std::size_t>(digit - U'0');
        if (index > ((std::numeric_limits<std::size_t>::max)() - value) / 10)
            return (std::numeric_limits<std::size_t>::max)();
        index = index * 10 + value;
    }
    return index;
}

class Parser {
public:
    Parser(std::u32string_view source, SnippetExpansionContext context)
        : source_(source), context_(context) {
        result_.text.reserve(source.size());
    }

    ParsedSnippetTemplate Parse() {
        ParseRange(0, source_.size());
        std::ranges::stable_sort(stops_, {}, [](IndexedStop const& entry) {
            return entry.stop.index == 0
                ? (std::numeric_limits<std::size_t>::max)()
                : entry.stop.index;
        });
        result_.tab_stops.reserve(stops_.size());
        for (auto const& entry : stops_) result_.tab_stops.push_back(entry.stop);
        return std::move(result_);
    }

private:
    void ParseRange(std::size_t begin, std::size_t end) {
        for (auto cursor = begin; cursor < end;) {
            if (source_[cursor] != U'$') {
                result_.text.push_back(source_[cursor++]);
                continue;
            }
            if (cursor + 1 < end && source_[cursor + 1] == U'$') {
                result_.text.push_back(U'$');
                cursor += 2;
                continue;
            }
            if (cursor + 1 < end && decimal_digit(source_[cursor + 1])) {
                cursor = ParseBareTabStop(cursor, end);
                continue;
            }
            if (cursor + 1 < end && variable_start(source_[cursor + 1])) {
                cursor = ParseBareVariable(cursor, end);
                continue;
            }
            if (cursor + 1 < end && source_[cursor + 1] == U'{') {
                auto next = ParseBraced(cursor, end);
                if (next != cursor) {
                    cursor = next;
                    continue;
                }
            }
            result_.text.push_back(source_[cursor++]);
        }
    }

    std::size_t ParseBareTabStop(std::size_t dollar, std::size_t end) {
        auto cursor = dollar + 1;
        while (cursor < end && decimal_digit(source_[cursor])) ++cursor;
        AddStop(*parse_index(source_.substr(dollar + 1, cursor - dollar - 1)),
            result_.text.size(), result_.text.size(), occurrence_++);
        return cursor;
    }

    std::size_t ParseBareVariable(std::size_t dollar, std::size_t end) {
        auto cursor = dollar + 1;
        while (cursor < end && variable_continue(source_[cursor])) ++cursor;
        auto name = source_.substr(dollar + 1, cursor - dollar - 1);
        if (name == U"TM_SELECTED_TEXT") {
            if (context_.selected_text) result_.text.append(*context_.selected_text);
        } else {
            result_.text.append(source_.substr(dollar, cursor - dollar));
        }
        return cursor;
    }

    std::size_t ParseBraced(std::size_t dollar, std::size_t end) {
        auto close = MatchingBrace(dollar + 1, end);
        if (!close) return dollar;
        auto payloadBegin = dollar + 2;
        auto payloadEnd = *close;
        auto colon = TopLevelColon(payloadBegin, payloadEnd);
        auto headEnd = colon.value_or(payloadEnd);
        auto head = source_.substr(payloadBegin, headEnd - payloadBegin);

        if (auto index = parse_index(head)) {
            auto occurrence = occurrence_++;
            auto rangeBegin = result_.text.size();
            if (colon) ParseRange(*colon + 1, payloadEnd);
            auto rangeEnd = result_.text.size();
            AddStop(*index, rangeBegin, rangeEnd, occurrence);
            return *close + 1;
        }

        if (head == U"TM_SELECTED_TEXT") {
            if (context_.selected_text) result_.text.append(*context_.selected_text);
            else if (colon) ParseRange(*colon + 1, payloadEnd);
            return *close + 1;
        }

        result_.text.append(source_.substr(dollar, *close + 1 - dollar));
        return *close + 1;
    }

    std::optional<std::size_t> MatchingBrace(std::size_t open, std::size_t end) const {
        std::size_t depth = 1;
        for (auto cursor = open + 1; cursor < end; ++cursor) {
            if (source_[cursor] == U'\\' && cursor + 1 < end
                && (source_[cursor + 1] == U'{' || source_[cursor + 1] == U'}')) {
                ++cursor;
                continue;
            }
            if (source_[cursor] == U'{') ++depth;
            else if (source_[cursor] == U'}' && --depth == 0) return cursor;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> TopLevelColon(
        std::size_t begin,
        std::size_t end) const {
        std::size_t depth = 0;
        for (auto cursor = begin; cursor < end; ++cursor) {
            if (source_[cursor] == U'\\' && cursor + 1 < end
                && (source_[cursor + 1] == U'{' || source_[cursor + 1] == U'}')) {
                ++cursor;
                continue;
            }
            if (source_[cursor] == U'{') ++depth;
            else if (source_[cursor] == U'}' && depth != 0) --depth;
            else if (source_[cursor] == U':' && depth == 0) return cursor;
        }
        return std::nullopt;
    }

    void AddStop(
        std::size_t index,
        std::size_t begin,
        std::size_t end,
        std::size_t occurrence) {
        stops_.push_back({SnippetTabStop{index, {begin, end}}, occurrence});
    }

    std::u32string_view source_;
    SnippetExpansionContext context_;
    ParsedSnippetTemplate result_;
    std::vector<IndexedStop> stops_;
    std::size_t occurrence_ = 0;
};

} // namespace snippet_detail

// Supported template syntax follows the useful, deterministic subset of VS Code
// snippets: $1, ${1}, ${1:default}, $0, and TM_SELECTED_TEXT. Existing $$
// escaping is retained because backslash escaping conflicts with LaTeX source.
// All ordinary characters, including newlines, are copied verbatim.
inline ParsedSnippetTemplate parse_snippet_template(
    std::u32string_view source,
    SnippetExpansionContext context = {}) {
    return snippet_detail::Parser(source, context).Parse();
}

} // namespace folia

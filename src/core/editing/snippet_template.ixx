// folia.core.snippet_template — source text and ordered tab stops for insertion actions.
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

// `$1`, `$2`, ... are removed from the inserted text and become caret stops.
// `$0` is the final stop. `$$` inserts a literal dollar. A dangling dollar is
// preserved verbatim so temporarily incomplete user templates remain lossless.
inline ParsedSnippetTemplate parse_snippet_template(std::u32string_view source) {
    ParsedSnippetTemplate result;
    result.text.reserve(source.size());
    std::size_t occurrence = 0;
    struct IndexedStop {
        SnippetTabStop stop;
        std::size_t occurrence = 0;
    };
    std::vector<IndexedStop> stops;

    for (std::size_t cursor = 0; cursor < source.size();) {
        if (source[cursor] != U'$') {
            result.text.push_back(source[cursor++]);
            continue;
        }
        if (cursor + 1 < source.size() && source[cursor + 1] == U'$') {
            result.text.push_back(U'$');
            cursor += 2;
            continue;
        }

        auto digit = cursor + 1;
        if (digit >= source.size() || source[digit] < U'0' || source[digit] > U'9') {
            result.text.push_back(source[cursor++]);
            continue;
        }
        std::size_t index = 0;
        while (digit < source.size() && source[digit] >= U'0' && source[digit] <= U'9') {
            auto value = static_cast<std::size_t>(source[digit] - U'0');
            if (index > ((std::numeric_limits<std::size_t>::max)() - value) / 10) {
                index = (std::numeric_limits<std::size_t>::max)();
            } else if (index != (std::numeric_limits<std::size_t>::max)()) {
                index = index * 10 + value;
            }
            ++digit;
        }
        auto offset = result.text.size();
        stops.push_back({SnippetTabStop{index, {offset, offset}}, occurrence++});
        cursor = digit;
    }

    std::ranges::stable_sort(stops, {}, [](IndexedStop const& entry) {
        return entry.stop.index == 0
            ? (std::numeric_limits<std::size_t>::max)()
            : entry.stop.index;
    });
    result.tab_stops.reserve(stops.size());
    for (auto const& entry : stops) result.tab_stops.push_back(entry.stop);
    return result;
}

} // namespace folia

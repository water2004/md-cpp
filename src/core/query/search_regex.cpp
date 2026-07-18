module;

#include <srell.hpp>

module folia.core.search;
import std;

namespace folia {

namespace {

std::u32string escape_regular_expression(std::u32string_view literal) {
    constexpr std::u32string_view metacharacters = U"\\^$.*+?()[]{}|/";
    std::u32string escaped;
    escaped.reserve(literal.size() * 2);
    for (auto character : literal) {
        if (metacharacters.find(character) != std::u32string_view::npos) {
            escaped.push_back(U'\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

SearchTextResult search_text_impl(
    std::u32string_view text,
    std::u32string_view query,
    SearchOptions options,
    std::u32string_view const* replacement_template) {
    SearchTextResult result;
    if (query.empty()) return result;
    try {
        auto pattern = options.regular_expression
            ? std::u32string(query)
            : escape_regular_expression(query);
        auto flags = srell::regex_constants::ECMAScript;
        if (!options.case_sensitive) flags |= srell::regex_constants::icase;
        srell::u32regex expression(pattern.data(), pattern.size(), flags);
        const auto* begin = text.data();
        const auto* end = begin + text.size();
        for (srell::u32cregex_iterator found(begin, end, expression), last;
             found != last;
             ++found) {
            auto const start = static_cast<std::size_t>((*found)[0].first - begin);
            auto const finish = static_cast<std::size_t>((*found)[0].second - begin);
            SearchTextMatch match;
            match.range = {start, finish};
            match.matched_text.assign((*found)[0].first, (*found)[0].second);
            if (replacement_template) {
                std::u32string expanded;
                found->format(
                    std::back_inserter(expanded),
                    replacement_template->data(),
                    replacement_template->data() + replacement_template->size());
                match.replacement = std::move(expanded);
            }
            result.matches.push_back(std::move(match));
        }
    } catch (srell::regex_error const& error) {
        result.error = "invalid regular expression (code "
            + std::to_string(static_cast<int>(error.code())) + ")";
    }
    return result;
}

} // namespace

SearchTextResult search_text(
    std::u32string_view text,
    std::u32string_view query,
    SearchOptions options) {
    return search_text_impl(text, query, options, nullptr);
}

SearchTextResult search_text_for_replacement(
    std::u32string_view text,
    std::u32string_view query,
    std::u32string_view replacement_template,
    SearchOptions options) {
    return search_text_impl(text, query, options, &replacement_template);
}

} // namespace folia

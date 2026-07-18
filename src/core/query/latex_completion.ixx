// folia.core.latex_completion — local LaTeX command prefix queries and ranking.
export module folia.core.latex_completion;
import std;
import folia.core.text_edit;

export namespace folia {

struct LatexCommandDefinition {
    std::string id;
    std::u32string trigger;
    std::u32string snippet;
    std::string description;
    std::string category;
    bool built_in = true;
    bool enabled = true;

    bool operator==(LatexCommandDefinition const&) const = default;
};

struct LatexCommandUsage {
    double score = 0.0;
    std::int64_t last_used_epoch_seconds = 0;

    bool operator==(LatexCommandUsage const&) const = default;
};

struct LatexCompletionCandidate {
    LatexCommandDefinition command;
    double recent_score = 0.0;
    bool exact_match = false;
    std::size_t catalog_order = 0;
};

struct LatexCompletionQuery {
    SourceRange replacement;
    std::u32string prefix;
    std::vector<LatexCompletionCandidate> candidates;
};

inline std::u32string_view latex_trigger_view(std::u32string_view trigger) {
    while (!trigger.empty() && trigger.front() == U'\\') trigger.remove_prefix(1);
    return trigger;
}

inline std::u32string normalize_latex_trigger(std::u32string_view trigger) {
    return std::u32string{latex_trigger_view(trigger)};
}

inline bool valid_latex_command_definition(LatexCommandDefinition const& command) {
    auto trigger = latex_trigger_view(command.trigger);
    return !command.id.empty()
        && !trigger.empty()
        && std::ranges::all_of(trigger, [](char32_t value) {
            return value > U' ' && value != U'\\';
        })
        && !command.snippet.empty();
}

inline std::u32string registered_latex_command_prefix(
    LatexCommandDefinition const& command) {
    return normalize_latex_trigger(command.trigger);
}

inline bool latex_command_slash_is_active(
    std::u32string_view source,
    std::size_t slash) {
    auto slash_run = slash;
    while (slash_run > 0 && source[slash_run - 1] == U'\\') --slash_run;
    return (slash - slash_run + 1) % 2 != 0;
}

inline double decayed_latex_usage_score(
    LatexCommandUsage const& usage,
    std::int64_t now_epoch_seconds,
    double half_life_seconds = 14.0 * 24.0 * 60.0 * 60.0) {
    if (usage.score <= 0.0) return 0.0;
    if (half_life_seconds <= 0.0) return usage.score;
    auto elapsed = static_cast<double>((std::max)(
        std::int64_t{0}, now_epoch_seconds - usage.last_used_epoch_seconds));
    return usage.score * std::exp2(-elapsed / half_life_seconds);
}

inline void record_latex_command_usage(
    LatexCommandUsage& usage,
    std::int64_t now_epoch_seconds,
    double half_life_seconds = 14.0 * 24.0 * 60.0 * 60.0) {
    usage.score = decayed_latex_usage_score(usage, now_epoch_seconds, half_life_seconds) + 1.0;
    usage.last_used_epoch_seconds = now_epoch_seconds;
}

inline std::vector<LatexCommandDefinition> merge_latex_command_catalog(
    std::span<LatexCommandDefinition const> built_in,
    std::span<LatexCommandDefinition const> custom) {
    std::vector<LatexCommandDefinition> result(built_in.begin(), built_in.end());
    for (auto command : custom) {
        command.built_in = false;
        auto found = std::ranges::find_if(result, [&](auto const& existing) {
            return existing.id == command.id
                || normalize_latex_trigger(existing.trigger) == normalize_latex_trigger(command.trigger);
        });
        if (found == result.end()) result.push_back(std::move(command));
        else *found = std::move(command);
    }
    return result;
}

inline std::optional<LatexCompletionQuery> query_latex_commands_at(
    std::span<LatexCommandDefinition const> catalog,
    std::u32string_view source,
    std::size_t caret,
    std::unordered_map<std::string, LatexCommandUsage> const& usage,
    std::int64_t now_epoch_seconds,
    std::size_t limit = 8) {
    if (caret > source.size()) return std::nullopt;

    auto maximum_length = std::size_t{0};
    for (auto const& command : catalog) {
        if (!command.enabled || !valid_latex_command_definition(command)) continue;
        maximum_length = (std::max)(maximum_length, latex_trigger_view(command.trigger).size());
    }
    if (maximum_length == 0) return std::nullopt;

    auto lower_bound = caret > maximum_length + 1
        ? caret - maximum_length - 1
        : std::size_t{0};
    std::optional<std::size_t> slash;
    std::u32string_view prefix;
    for (auto cursor = caret; cursor > lower_bound;) {
        --cursor;
        if (source[cursor] != U'\\' || !latex_command_slash_is_active(source, cursor))
            continue;
        auto candidate_prefix = source.substr(cursor + 1, caret - cursor - 1);
        auto matches = false;
        for (auto const& command : catalog) {
            if (!command.enabled || !valid_latex_command_definition(command)) continue;
            if (latex_trigger_view(command.trigger).starts_with(candidate_prefix)) {
                matches = true;
                break;
            }
        }
        if (!matches) continue;
        slash = cursor;
        prefix = candidate_prefix;
        break;
    }
    if (!slash) return std::nullopt;

    std::vector<LatexCompletionCandidate> result;
    result.reserve((std::min)(catalog.size(), limit));
    std::vector<std::u32string_view> matching_triggers;
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        auto const& command = catalog[index];
        if (!command.enabled || !valid_latex_command_definition(command)) continue;
        auto trigger = latex_trigger_view(command.trigger);
        if (!trigger.starts_with(prefix)) continue;
        matching_triggers.push_back(trigger);
        auto found_usage = usage.find(command.id);
        auto score = found_usage == usage.end()
            ? 0.0
            : decayed_latex_usage_score(found_usage->second, now_epoch_seconds);
        result.push_back({
            .command = command,
            .recent_score = score,
            .exact_match = trigger == prefix,
            .catalog_order = index,
        });
    }

    auto replacement_end = caret;
    auto matched_length = prefix.size();
    while (replacement_end < source.size() && !matching_triggers.empty()) {
        if (std::ranges::any_of(matching_triggers, [&](auto trigger) {
            return trigger.size() == matched_length;
        })) break;
        std::vector<std::u32string_view> continued;
        for (auto trigger : matching_triggers) {
            if (trigger.size() > matched_length
                && trigger[matched_length] == source[replacement_end])
                continued.push_back(trigger);
        }
        if (continued.empty()) break;
        matching_triggers = std::move(continued);
        ++replacement_end;
        ++matched_length;
    }

    std::ranges::stable_sort(result, [](auto const& left, auto const& right) {
        if (left.exact_match != right.exact_match) return left.exact_match > right.exact_match;
        if (left.recent_score != right.recent_score) return left.recent_score > right.recent_score;
        return left.catalog_order < right.catalog_order;
    });
    if (result.size() > limit) result.resize(limit);
    return LatexCompletionQuery{
        .replacement = {*slash, replacement_end},
        .prefix = std::u32string{prefix},
        .candidates = std::move(result),
    };
}

} // namespace folia

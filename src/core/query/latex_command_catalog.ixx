// folia.core.latex_command_catalog — mutable, platform-independent command catalog state.
export module folia.core.latex_command_catalog;
import std;
export import folia.core.latex_completion;
import folia.core.utf;

export namespace folia {

enum class LatexCatalogMutationError {
    None,
    DuplicateCustomTrigger,
    InvalidDefinition,
    MissingCustomCommand,
};

struct LatexCatalogMutationResult {
    LatexCatalogMutationError error = LatexCatalogMutationError::None;
    std::string command_id;

    bool ok() const { return error == LatexCatalogMutationError::None; }
};

class LatexCommandCatalogState {
public:
    LatexCommandCatalogState() = default;

    LatexCommandCatalogState(
        std::vector<LatexCommandDefinition> built_in,
        std::vector<LatexCommandDefinition> custom,
        std::unordered_map<std::string, LatexCommandUsage> usage = {}) {
        Reset(std::move(built_in), std::move(custom), std::move(usage));
    }

    void Reset(
        std::vector<LatexCommandDefinition> built_in,
        std::vector<LatexCommandDefinition> custom,
        std::unordered_map<std::string, LatexCommandUsage> usage = {}) {
        for (auto& command : built_in) command.built_in = true;
        for (auto& command : custom) command.built_in = false;
        built_in_ = std::move(built_in);
        custom_ = std::move(custom);
        usage_ = std::move(usage);
        Rebuild();
    }

    std::vector<LatexCommandDefinition> const& Commands() const { return commands_; }
    std::vector<LatexCommandDefinition> const& CustomCommands() const { return custom_; }
    std::unordered_map<std::string, LatexCommandUsage> const& Usage() const { return usage_; }

    LatexCatalogMutationResult AddCustom(
        std::u32string trigger,
        std::u32string snippet,
        std::string description) {
        trigger = normalize_latex_trigger(trigger);
        if (HasCustomTrigger(trigger))
            return {.error = LatexCatalogMutationError::DuplicateCustomTrigger};

        LatexCommandDefinition command{
            .id = NextCustomId(trigger),
            .trigger = std::move(trigger),
            .snippet = std::move(snippet),
            .description = std::move(description),
            .category = "custom",
            .built_in = false,
        };
        if (!valid_latex_command_definition(command))
            return {.error = LatexCatalogMutationError::InvalidDefinition};

        auto id = command.id;
        custom_.push_back(std::move(command));
        Rebuild();
        return {.command_id = std::move(id)};
    }

    LatexCatalogMutationResult UpdateCustom(
        std::string_view id,
        std::u32string trigger,
        std::u32string snippet,
        std::string description) {
        auto found = std::ranges::find(custom_, id, &LatexCommandDefinition::id);
        if (found == custom_.end())
            return {.error = LatexCatalogMutationError::MissingCustomCommand};

        trigger = normalize_latex_trigger(trigger);
        if (std::ranges::any_of(custom_, [&](auto const& command) {
            return command.id != id && command.trigger == trigger;
        })) return {.error = LatexCatalogMutationError::DuplicateCustomTrigger};

        auto updated = *found;
        updated.trigger = std::move(trigger);
        updated.snippet = std::move(snippet);
        updated.description = std::move(description);
        if (!valid_latex_command_definition(updated))
            return {.error = LatexCatalogMutationError::InvalidDefinition};

        *found = std::move(updated);
        Rebuild();
        return {.command_id = std::string{id}};
    }

    LatexCatalogMutationResult RemoveCustom(std::string_view id) {
        auto found = std::ranges::find(custom_, id, &LatexCommandDefinition::id);
        if (found == custom_.end())
            return {.error = LatexCatalogMutationError::MissingCustomCommand};
        auto removed_id = found->id;
        usage_.erase(removed_id);
        custom_.erase(found);
        Rebuild();
        return {.command_id = std::move(removed_id)};
    }

    void ResetUsage() { usage_.clear(); }

    bool RecordUsage(std::string_view id, std::int64_t now_epoch_seconds) {
        if (std::ranges::find(commands_, id, &LatexCommandDefinition::id) == commands_.end())
            return false;
        record_latex_command_usage(usage_[std::string{id}], now_epoch_seconds);
        return true;
    }

    double RecentScore(std::string_view id, std::int64_t now_epoch_seconds) const {
        auto found = usage_.find(std::string{id});
        return found == usage_.end()
            ? 0.0
            : decayed_latex_usage_score(found->second, now_epoch_seconds);
    }

    std::optional<LatexCompletionQuery> QueryAt(
        std::u32string_view source,
        std::size_t caret,
        std::int64_t now_epoch_seconds,
        std::size_t limit = 8) const {
        return query_latex_commands_at(
            commands_, source, caret, usage_, now_epoch_seconds, limit);
    }

private:
    bool HasCustomTrigger(std::u32string_view trigger) const {
        return std::ranges::any_of(custom_, [&](auto const& command) {
            return command.trigger == trigger;
        });
    }

    std::string NextCustomId(std::u32string_view trigger) const {
        auto base = "user." + cps_to_utf8(trigger);
        auto id = base;
        for (std::size_t suffix = 2;
             std::ranges::any_of(custom_, [&](auto const& command) { return command.id == id; });
             ++suffix)
            id = base + "." + std::to_string(suffix);
        return id;
    }

    void Rebuild() {
        commands_ = merge_latex_command_catalog(built_in_, custom_);
    }

    std::vector<LatexCommandDefinition> built_in_;
    std::vector<LatexCommandDefinition> custom_;
    std::vector<LatexCommandDefinition> commands_;
    std::unordered_map<std::string, LatexCommandUsage> usage_;
};

} // namespace folia

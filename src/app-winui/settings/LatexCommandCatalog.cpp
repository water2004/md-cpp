#include "pch.h"
#include "settings/LatexCommandCatalog.h"
#include "localization/Localization.h"

namespace winrt::Folia
{
    LatexCommandCatalog::LatexCommandCatalog()
    {
        auto stored = store_.Load();
        diagnostics_ = std::move(stored.diagnostics);
        state_.Reset(
            std::move(stored.builtIn),
            std::move(stored.custom),
            std::move(stored.usage));
    }

    std::optional<hstring> LatexCommandCatalog::AddCustom(
        std::u32string trigger,
        std::u32string snippet,
        std::string description)
    {
        return CommitMutation(state_.AddCustom(
            std::move(trigger), std::move(snippet), std::move(description)));
    }

    std::optional<hstring> LatexCommandCatalog::UpdateCustom(
        std::string_view id,
        std::u32string trigger,
        std::u32string snippet,
        std::string description)
    {
        return CommitMutation(state_.UpdateCustom(
            id, std::move(trigger), std::move(snippet), std::move(description)));
    }

    std::optional<hstring> LatexCommandCatalog::RemoveCustom(std::string_view id)
    {
        return CommitMutation(state_.RemoveCustom(id));
    }

    std::optional<hstring> LatexCommandCatalog::ResetUsage()
    {
        state_.ResetUsage();
        usageChangesSinceFlush_ = 0;
        dirty_ = true;
        return SaveUserState();
    }

    std::optional<folia::LatexCompletionQuery> LatexCommandCatalog::QueryAt(
        std::u32string_view source,
        std::size_t caret,
        std::size_t limit) const
    {
        return state_.QueryAt(source, caret, NowSeconds(), limit);
    }

    void LatexCommandCatalog::RecordUsage(std::string_view id)
    {
        if (!state_.RecordUsage(id, NowSeconds())) return;
        dirty_ = true;
        if (++usageChangesSinceFlush_ >= 8) SaveUserState();
    }

    double LatexCommandCatalog::RecentScore(std::string_view id) const
    {
        return state_.RecentScore(id, NowSeconds());
    }

    std::optional<hstring> LatexCommandCatalog::Flush()
    {
        return dirty_ ? SaveUserState() : std::nullopt;
    }

    std::optional<hstring> LatexCommandCatalog::SaveUserState()
    {
        auto error = store_.Save(state_.CustomCommands(), state_.Usage());
        if (!error)
        {
            dirty_ = false;
            usageChangesSinceFlush_ = 0;
        }
        return error;
    }

    std::optional<hstring> LatexCommandCatalog::CommitMutation(
        folia::LatexCatalogMutationResult const& result)
    {
        if (!result.ok()) return MutationError(result.error);
        dirty_ = true;
        return SaveUserState();
    }

    hstring LatexCommandCatalog::MutationError(folia::LatexCatalogMutationError error) const
    {
        using folia::LatexCatalogMutationError;
        switch (error)
        {
            case LatexCatalogMutationError::DuplicateCustomTrigger:
                return Localize(L"LatexDuplicateTriggerError");
            case LatexCatalogMutationError::InvalidDefinition:
                return Localize(L"LatexInvalidCommandError");
            case LatexCatalogMutationError::MissingCustomCommand:
                return Localize(L"LatexMissingCustomError");
            case LatexCatalogMutationError::None:
                return {};
        }
        return Localize(L"LatexInvalidCommandError");
    }

    std::int64_t LatexCommandCatalog::NowSeconds() const
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

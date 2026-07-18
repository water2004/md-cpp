#include "pch.h"
#include "settings/LatexCommandCatalog.h"
#include "storage/AssetPaths.h"

import folia.core.utf;

namespace
{
    std::string ReadUtf8(std::filesystem::path const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open LaTeX command catalog");
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    void WriteUtf8(std::filesystem::path const& path, std::string_view value)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("cannot create LaTeX command settings");
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
        stream.flush();
        if (!stream) throw std::runtime_error("cannot write LaTeX command settings");
    }

    folia::LatexCommandDefinition ParseCommand(
        winrt::Windows::Data::Json::JsonObject const& value,
        bool builtIn)
    {
        folia::LatexCommandDefinition command{
            .id = winrt::to_string(value.GetNamedString(L"id")),
            .trigger = folia::utf8_to_cps(winrt::to_string(value.GetNamedString(L"trigger"))),
            .snippet = folia::utf8_to_cps(winrt::to_string(value.GetNamedString(L"snippet"))),
            .description = winrt::to_string(value.GetNamedString(L"description", L"")),
            .category = winrt::to_string(value.GetNamedString(L"category", L"")),
            .built_in = builtIn,
            .enabled = value.GetNamedBoolean(L"enabled", true),
        };
        command.trigger = folia::normalize_latex_trigger(command.trigger);
        if (!folia::valid_latex_command_definition(command))
            throw std::runtime_error("invalid LaTeX command definition");
        return command;
    }

    winrt::Windows::Data::Json::JsonObject SerializeCommand(
        folia::LatexCommandDefinition const& command)
    {
        using winrt::Windows::Data::Json::JsonValue;
        winrt::Windows::Data::Json::JsonObject value;
        value.Insert(L"id", JsonValue::CreateStringValue(winrt::to_hstring(command.id)));
        value.Insert(L"trigger", JsonValue::CreateStringValue(
            winrt::to_hstring(folia::cps_to_utf8(command.trigger))));
        value.Insert(L"snippet", JsonValue::CreateStringValue(
            winrt::to_hstring(folia::cps_to_utf8(command.snippet))));
        value.Insert(L"description", JsonValue::CreateStringValue(
            winrt::to_hstring(command.description)));
        value.Insert(L"category", JsonValue::CreateStringValue(
            winrt::to_hstring(command.category)));
        value.Insert(L"enabled", JsonValue::CreateBooleanValue(command.enabled));
        return value;
    }
}

namespace winrt::Folia
{
    LatexCommandCatalog::LatexCommandCatalog()
    {
        try { LoadBuiltIns(); }
        catch (std::exception const& error)
        {
            diagnostics_.push_back(winrt::to_hstring(error.what()));
        }
        try { LoadUserState(); }
        catch (std::exception const& error)
        {
            diagnostics_.push_back(winrt::to_hstring(error.what()));
            custom_.clear();
            usage_.clear();
        }
        Rebuild();
    }

    void LatexCommandCatalog::LoadBuiltIns()
    {
        auto path = AssetPath(std::filesystem::path(L"latex") / L"commands.json");
        auto root = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(ReadUtf8(path)));
        if (root.GetNamedNumber(L"schemaVersion") != 1.0)
            throw std::runtime_error("unsupported LaTeX command catalog schema");
        std::unordered_set<std::string> ids;
        std::unordered_set<std::u32string> triggers;
        for (auto const& entry : root.GetNamedArray(L"commands"))
        {
            auto command = ParseCommand(entry.GetObject(), true);
            if (!ids.insert(command.id).second || !triggers.insert(command.trigger).second)
                throw std::runtime_error("duplicate built-in LaTeX command");
            builtIn_.push_back(std::move(command));
        }
    }

    void LatexCommandCatalog::LoadUserState()
    {
        auto path = AssetPath(std::filesystem::path(L"latex") / L"commands.user.json");
        if (!std::filesystem::exists(path)) return;
        auto root = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(ReadUtf8(path)));
        if (root.GetNamedNumber(L"schemaVersion") != 1.0)
            throw std::runtime_error("unsupported user LaTeX command schema");
        std::unordered_set<std::string> ids;
        std::unordered_set<std::u32string> triggers;
        for (auto const& entry : root.GetNamedArray(L"customCommands"))
        {
            auto command = ParseCommand(entry.GetObject(), false);
            if (!ids.insert(command.id).second || !triggers.insert(command.trigger).second)
                throw std::runtime_error("duplicate custom LaTeX command");
            custom_.push_back(std::move(command));
        }
        if (!root.HasKey(L"usage")) return;
        for (auto const& entry : root.GetNamedArray(L"usage"))
        {
            auto value = entry.GetObject();
            auto id = winrt::to_string(value.GetNamedString(L"id"));
            auto score = value.GetNamedNumber(L"score");
            auto lastUsed = value.GetNamedNumber(L"lastUsed");
            if (id.empty() || score < 0.0 || lastUsed < 0.0) continue;
            usage_[std::move(id)] = {
                .score = score,
                .last_used_epoch_seconds = static_cast<std::int64_t>(lastUsed),
            };
        }
    }

    void LatexCommandCatalog::Rebuild()
    {
        commands_ = folia::merge_latex_command_catalog(builtIn_, custom_);
        ++revision_;
    }

    std::optional<hstring> LatexCommandCatalog::AddCustom(
        std::u32string trigger,
        std::u32string snippet,
        std::string description)
    {
        trigger = folia::normalize_latex_trigger(trigger);
        if (std::ranges::any_of(custom_, [&](auto const& value) { return value.trigger == trigger; }))
            return L"A custom command with this trigger already exists.";
        auto id = "user." + folia::cps_to_utf8(trigger);
        for (std::size_t suffix = 2;
             std::ranges::any_of(custom_, [&](auto const& value) { return value.id == id; });
             ++suffix)
            id = "user." + folia::cps_to_utf8(trigger) + "." + std::to_string(suffix);
        folia::LatexCommandDefinition command{
            .id = std::move(id),
            .trigger = std::move(trigger),
            .snippet = std::move(snippet),
            .description = std::move(description),
            .category = "custom",
            .built_in = false,
        };
        if (!folia::valid_latex_command_definition(command))
            return L"Command names must contain letters only, and the insertion template cannot be empty.";
        custom_.push_back(std::move(command));
        Rebuild();
        dirty_ = true;
        return SaveUserState();
    }

    std::optional<hstring> LatexCommandCatalog::UpdateCustom(
        std::string_view id,
        std::u32string trigger,
        std::u32string snippet,
        std::string description)
    {
        auto found = std::ranges::find(custom_, id, &folia::LatexCommandDefinition::id);
        if (found == custom_.end()) return L"The custom command no longer exists.";
        trigger = folia::normalize_latex_trigger(trigger);
        if (std::ranges::any_of(custom_, [&](auto const& value) {
            return value.id != id && value.trigger == trigger;
        })) return L"A custom command with this trigger already exists.";
        auto updated = *found;
        updated.trigger = std::move(trigger);
        updated.snippet = std::move(snippet);
        updated.description = std::move(description);
        if (!folia::valid_latex_command_definition(updated))
            return L"Command names must contain letters only, and the insertion template cannot be empty.";
        *found = std::move(updated);
        Rebuild();
        dirty_ = true;
        return SaveUserState();
    }

    std::optional<hstring> LatexCommandCatalog::RemoveCustom(std::string_view id)
    {
        auto found = std::ranges::find(custom_, id, &folia::LatexCommandDefinition::id);
        if (found == custom_.end()) return L"Only custom commands can be removed.";
        usage_.erase(found->id);
        custom_.erase(found);
        Rebuild();
        dirty_ = true;
        return SaveUserState();
    }

    std::optional<hstring> LatexCommandCatalog::ResetUsage()
    {
        usage_.clear();
        usageChangesSinceFlush_ = 0;
        dirty_ = true;
        ++revision_;
        return SaveUserState();
    }

    std::vector<folia::LatexCompletionCandidate> LatexCommandCatalog::Query(
        std::u32string_view prefix,
        std::size_t limit) const
    {
        return folia::query_latex_commands(
            commands_, prefix, usage_, NowSeconds(), limit);
    }

    void LatexCommandCatalog::RecordUsage(std::string_view id)
    {
        auto found = std::ranges::find(commands_, id, &folia::LatexCommandDefinition::id);
        if (found == commands_.end()) return;
        folia::record_latex_command_usage(usage_[std::string{id}], NowSeconds());
        dirty_ = true;
        ++revision_;
        if (++usageChangesSinceFlush_ >= 8) SaveUserState();
    }

    double LatexCommandCatalog::RecentScore(std::string_view id) const
    {
        auto found = usage_.find(std::string{id});
        return found == usage_.end()
            ? 0.0
            : folia::decayed_latex_usage_score(found->second, NowSeconds());
    }

    std::optional<hstring> LatexCommandCatalog::Flush()
    {
        return dirty_ ? SaveUserState() : std::nullopt;
    }

    std::optional<hstring> LatexCommandCatalog::SaveUserState()
    {
        try
        {
            auto directory = AssetPath(L"latex");
            std::filesystem::create_directories(directory);
            Windows::Data::Json::JsonObject root;
            root.Insert(L"schemaVersion", Windows::Data::Json::JsonValue::CreateNumberValue(1));
            Windows::Data::Json::JsonArray custom;
            for (auto const& command : custom_) custom.Append(SerializeCommand(command));
            root.Insert(L"customCommands", custom);
            Windows::Data::Json::JsonArray usage;
            for (auto const& [id, statistics] : usage_)
            {
                Windows::Data::Json::JsonObject value;
                value.Insert(L"id", Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(id)));
                value.Insert(L"score", Windows::Data::Json::JsonValue::CreateNumberValue(statistics.score));
                value.Insert(L"lastUsed", Windows::Data::Json::JsonValue::CreateNumberValue(
                    static_cast<double>(statistics.last_used_epoch_seconds)));
                usage.Append(value);
            }
            root.Insert(L"usage", usage);
            auto target = directory / L"commands.user.json";
            auto temporary = directory / L"commands.user.json.tmp";
            WriteUtf8(temporary, winrt::to_string(root.Stringify()));
            if (!MoveFileExW(temporary.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                winrt::throw_last_error();
            dirty_ = false;
            usageChangesSinceFlush_ = 0;
            return std::nullopt;
        }
        catch (winrt::hresult_error const& error) { return error.message(); }
        catch (std::exception const& error) { return winrt::to_hstring(error.what()); }
    }

    std::int64_t LatexCommandCatalog::NowSeconds() const
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

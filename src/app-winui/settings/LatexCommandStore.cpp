#include "pch.h"
#include "settings/LatexCommandStore.h"
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

    template<typename Range>
    void ValidateUniqueCommands(Range const& commands, std::string_view kind)
    {
        std::unordered_set<std::string> ids;
        std::unordered_set<std::u32string> triggers;
        for (auto const& command : commands)
        {
            if (!ids.insert(command.id).second || !triggers.insert(command.trigger).second)
                throw std::runtime_error("duplicate " + std::string{kind} + " LaTeX command");
        }
    }
}

namespace winrt::Folia
{
    LatexCommandStoredState LatexCommandStore::Load() const
    {
        LatexCommandStoredState result;
        try { result.builtIn = LoadBuiltIns(); }
        catch (std::exception const& error)
        {
            result.diagnostics.push_back(winrt::to_hstring(error.what()));
        }
        try { LoadUserState(result.custom, result.usage); }
        catch (std::exception const& error)
        {
            result.diagnostics.push_back(winrt::to_hstring(error.what()));
            result.custom.clear();
            result.usage.clear();
        }
        return result;
    }

    std::vector<folia::LatexCommandDefinition> LatexCommandStore::LoadBuiltIns() const
    {
        auto path = AssetPath(std::filesystem::path(L"latex") / L"commands.json");
        auto root = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(ReadUtf8(path)));
        if (root.GetNamedNumber(L"schemaVersion") != 1.0)
            throw std::runtime_error("unsupported LaTeX command catalog schema");

        std::vector<folia::LatexCommandDefinition> commands;
        for (auto const& entry : root.GetNamedArray(L"commands"))
            commands.push_back(ParseCommand(entry.GetObject(), true));
        ValidateUniqueCommands(commands, "built-in");
        return commands;
    }

    void LatexCommandStore::LoadUserState(
        std::vector<folia::LatexCommandDefinition>& custom,
        std::unordered_map<std::string, folia::LatexCommandUsage>& usage) const
    {
        auto path = AssetPath(std::filesystem::path(L"latex") / L"commands.user.json");
        if (!std::filesystem::exists(path)) return;
        auto root = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(ReadUtf8(path)));
        if (root.GetNamedNumber(L"schemaVersion") != 1.0)
            throw std::runtime_error("unsupported user LaTeX command schema");

        for (auto const& entry : root.GetNamedArray(L"customCommands"))
            custom.push_back(ParseCommand(entry.GetObject(), false));
        ValidateUniqueCommands(custom, "custom");

        if (!root.HasKey(L"usage")) return;
        for (auto const& entry : root.GetNamedArray(L"usage"))
        {
            auto value = entry.GetObject();
            auto id = winrt::to_string(value.GetNamedString(L"id"));
            auto score = value.GetNamedNumber(L"score");
            auto lastUsed = value.GetNamedNumber(L"lastUsed");
            if (id.empty() || score < 0.0 || lastUsed < 0.0) continue;
            usage[std::move(id)] = {
                .score = score,
                .last_used_epoch_seconds = static_cast<std::int64_t>(lastUsed),
            };
        }
    }

    std::optional<hstring> LatexCommandStore::Save(
        std::span<folia::LatexCommandDefinition const> customCommands,
        std::unordered_map<std::string, folia::LatexCommandUsage> const& usageStatistics) const
    {
        try
        {
            auto directory = AssetPath(L"latex");
            std::filesystem::create_directories(directory);
            Windows::Data::Json::JsonObject root;
            root.Insert(L"schemaVersion", Windows::Data::Json::JsonValue::CreateNumberValue(1));

            Windows::Data::Json::JsonArray custom;
            for (auto const& command : customCommands) custom.Append(SerializeCommand(command));
            root.Insert(L"customCommands", custom);

            std::vector<std::string_view> usageIds;
            usageIds.reserve(usageStatistics.size());
            for (auto const& [id, statistics] : usageStatistics)
                if (statistics.score >= 0.0 && statistics.last_used_epoch_seconds >= 0)
                    usageIds.push_back(id);
            std::ranges::sort(usageIds);
            Windows::Data::Json::JsonArray usage;
            for (auto id : usageIds)
            {
                auto const& statistics = usageStatistics.at(std::string{id});
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
            return std::nullopt;
        }
        catch (winrt::hresult_error const& error) { return error.message(); }
        catch (std::exception const& error) { return winrt::to_hstring(error.what()); }
    }
}

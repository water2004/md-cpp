#include "pch.h"
#include "settings/AppSettings.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"

import folia.core.utf;
import folia.platform.editor_shortcuts;

namespace
{
    std::string ReadUtf8(std::filesystem::path const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open settings file");
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    void WriteUtf8(std::filesystem::path const& path, std::string_view value)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("cannot create settings file");
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
        stream.flush();
        if (!stream) throw std::runtime_error("cannot write settings file");
    }

    using namespace folia::platform::editor;

    std::string ScopeName(EditorShortcutScope scope)
    {
        switch (scope)
        {
            case EditorShortcutScope::Global: return "global";
            case EditorShortcutScope::Code: return "code";
            case EditorShortcutScope::Math: return "math";
        }
        throw std::runtime_error("unsupported shortcut scope");
    }

    EditorShortcutScope ParseScope(winrt::hstring const& value)
    {
        if (value == L"global") return EditorShortcutScope::Global;
        if (value == L"code") return EditorShortcutScope::Code;
        if (value == L"math") return EditorShortcutScope::Math;
        throw std::runtime_error("unsupported shortcut scope");
    }

    winrt::Windows::Data::Json::JsonObject SerializeShortcut(EditorShortcutBinding const& binding)
    {
        using winrt::Windows::Data::Json::JsonValue;
        winrt::Windows::Data::Json::JsonObject value;
        value.Insert(L"id", JsonValue::CreateStringValue(winrt::to_hstring(binding.id)));
        value.Insert(L"actionId", JsonValue::CreateStringValue(winrt::to_hstring(binding.action_id)));
        value.Insert(L"customName", JsonValue::CreateStringValue(winrt::to_hstring(binding.custom_name)));
        value.Insert(L"actionKind", JsonValue::CreateStringValue(
            binding.action_kind == EditorShortcutActionKind::BuiltIn ? L"builtin" : L"snippet"));
        value.Insert(L"scope", JsonValue::CreateStringValue(winrt::to_hstring(ScopeName(binding.scope))));
        value.Insert(L"snippet", JsonValue::CreateStringValue(
            winrt::to_hstring(folia::cps_to_utf8(binding.snippet))));
        if (!binding.gesture)
        {
            value.Insert(L"gesture", JsonValue::CreateNullValue());
            return value;
        }
        winrt::Windows::Data::Json::JsonObject gesture;
        gesture.Insert(L"key", JsonValue::CreateNumberValue(
            static_cast<std::uint32_t>(binding.gesture->key)));
        gesture.Insert(L"control", JsonValue::CreateBooleanValue(binding.gesture->control));
        gesture.Insert(L"shift", JsonValue::CreateBooleanValue(binding.gesture->shift));
        gesture.Insert(L"alt", JsonValue::CreateBooleanValue(binding.gesture->alt));
        value.Insert(L"gesture", gesture);
        return value;
    }

    EditorShortcutBinding ParseShortcut(winrt::Windows::Data::Json::JsonObject const& value)
    {
        using winrt::Windows::Data::Json::JsonValueType;
        EditorShortcutBinding binding;
        binding.id = winrt::to_string(value.GetNamedString(L"id"));
        binding.action_id = winrt::to_string(value.GetNamedString(L"actionId"));
        binding.custom_name = winrt::to_string(value.GetNamedString(L"customName", L""));
        auto kind = value.GetNamedString(L"actionKind");
        if (kind == L"builtin") binding.action_kind = EditorShortcutActionKind::BuiltIn;
        else if (kind == L"snippet") binding.action_kind = EditorShortcutActionKind::InsertSnippet;
        else throw std::runtime_error("unsupported shortcut action kind");
        binding.scope = ParseScope(value.GetNamedString(L"scope"));
        binding.snippet = folia::utf8_to_cps(winrt::to_string(value.GetNamedString(L"snippet", L"")));
        auto gestureValue = value.GetNamedValue(L"gesture");
        if (gestureValue.ValueType() != JsonValueType::Null)
        {
            auto gesture = gestureValue.GetObject();
            auto key = gesture.GetNamedNumber(L"key");
            if (key < 0 || key > 255 || std::floor(key) != key)
                throw std::runtime_error("invalid shortcut key");
            binding.gesture = EditorKeyGesture{
                .key = static_cast<EditorKey>(static_cast<std::uint32_t>(key)),
                .control = gesture.GetNamedBoolean(L"control", false),
                .shift = gesture.GetNamedBoolean(L"shift", false),
                .alt = gesture.GetNamedBoolean(L"alt", false),
            };
        }
        if (binding.id.empty() || binding.action_id.empty())
            throw std::runtime_error("shortcut identifiers cannot be empty");
        if (binding.action_kind == EditorShortcutActionKind::InsertSnippet
            && (binding.custom_name.empty() || binding.snippet.empty()))
            throw std::runtime_error("custom shortcut name and snippet cannot be empty");
        return binding;
    }

    std::vector<EditorShortcutBinding> ParseShortcuts(
        winrt::Windows::Data::Json::JsonArray const& values)
    {
        auto result = default_editor_shortcuts();
        std::unordered_set<std::string> seen;
        for (auto const& value : values)
        {
            auto parsed = ParseShortcut(value.GetObject());
            if (!seen.insert(parsed.id).second) throw std::runtime_error("duplicate shortcut id");
            auto existing = std::ranges::find(result, parsed.id, &EditorShortcutBinding::id);
            if (existing != result.end()) *existing = std::move(parsed);
            else result.push_back(std::move(parsed));
        }
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            if (!result[index].gesture) continue;
            if (find_editor_shortcut_conflict(
                result, *result[index].gesture, result[index].scope, index))
                throw std::runtime_error("conflicting shortcut gestures");
        }
        return result;
    }
}

namespace winrt::Folia
{
    SettingsLoadResult LoadAppSettings()
    {
        auto path = AssetPath(L"settings.json");
        if (!std::filesystem::exists(path)) return {};
        try
        {
            auto root = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(ReadUtf8(path)));
            auto schemaVersion = root.GetNamedNumber(L"schemaVersion");
            if (schemaVersion != 1.0 && schemaVersion != 2.0 && schemaVersion != 3.0)
                throw std::runtime_error("unsupported settings schema version");
            AppSettings settings;
            settings.mathRenderingEnabled = root.GetNamedBoolean(L"mathRenderingEnabled");
            settings.themeId = winrt::to_string(root.GetNamedString(L"themeId"));
            if (schemaVersion >= 2.0)
                settings.languageId = winrt::to_string(root.GetNamedString(L"languageId"));
            if (schemaVersion >= 3.0)
                settings.shortcutBindings = ParseShortcuts(root.GetNamedArray(L"shortcutBindings"));
            if (settings.themeId.empty()) throw std::runtime_error("themeId cannot be empty");
            if (!IsSupportedLanguage(settings.languageId))
                throw std::runtime_error("unsupported languageId");
            return { std::move(settings), true, {} };
        }
        catch (winrt::hresult_error const& error)
        {
            return { {}, false, LocalizeFormat(L"SettingsFallback", { error.message() }) };
        }
        catch (std::exception const& error)
        {
            return { {}, false, LocalizeFormat(L"SettingsFallback", { winrt::to_hstring(error.what()) }) };
        }
    }

    std::optional<winrt::hstring> SaveAppSettings(AppSettings const& settings)
    {
        try
        {
            auto directory = AssetsDirectory();
            std::filesystem::create_directories(directory);
            Windows::Data::Json::JsonObject root;
            root.Insert(L"schemaVersion", Windows::Data::Json::JsonValue::CreateNumberValue(3));
            root.Insert(L"mathRenderingEnabled", Windows::Data::Json::JsonValue::CreateBooleanValue(settings.mathRenderingEnabled));
            root.Insert(L"themeId", Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(settings.themeId)));
            root.Insert(L"languageId", Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(settings.languageId)));
            Windows::Data::Json::JsonArray shortcuts;
            for (auto const& binding : settings.shortcutBindings)
                shortcuts.Append(SerializeShortcut(binding));
            root.Insert(L"shortcutBindings", shortcuts);
            auto target = directory / L"settings.json";
            auto temporary = directory / L"settings.json.tmp";
            WriteUtf8(temporary, winrt::to_string(root.Stringify()));
            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                winrt::throw_last_error();
            return std::nullopt;
        }
        catch (winrt::hresult_error const& error)
        {
            return LocalizeFormat(L"UnableSaveSettings", { error.message() });
        }
        catch (std::exception const& error)
        {
            return LocalizeFormat(L"UnableSaveSettings", { winrt::to_hstring(error.what()) });
        }
    }
}

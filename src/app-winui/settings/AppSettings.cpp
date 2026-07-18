#include "pch.h"
#include "settings/AppSettings.h"
#include "settings/ShortcutSettingsSerialization.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"

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
            if (schemaVersion != 1.0 && schemaVersion != 2.0
                && schemaVersion != 3.0 && schemaVersion != 4.0)
                throw std::runtime_error("unsupported settings schema version");
            AppSettings settings;
            settings.mathRenderingEnabled = root.GetNamedBoolean(L"mathRenderingEnabled");
            if (schemaVersion >= 4.0)
                settings.latexSuggestionsEnabled = root.GetNamedBoolean(L"latexSuggestionsEnabled", true);
            settings.themeId = winrt::to_string(root.GetNamedString(L"themeId"));
            if (schemaVersion >= 2.0)
                settings.languageId = winrt::to_string(root.GetNamedString(L"languageId"));
            if (schemaVersion >= 3.0)
                settings.shortcutBindings = ParseShortcutSettings(root.GetNamedArray(L"shortcutBindings"));
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
            root.Insert(L"schemaVersion", Windows::Data::Json::JsonValue::CreateNumberValue(4));
            root.Insert(L"mathRenderingEnabled", Windows::Data::Json::JsonValue::CreateBooleanValue(settings.mathRenderingEnabled));
            root.Insert(L"latexSuggestionsEnabled", Windows::Data::Json::JsonValue::CreateBooleanValue(settings.latexSuggestionsEnabled));
            root.Insert(L"themeId", Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(settings.themeId)));
            root.Insert(L"languageId", Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(settings.languageId)));
            root.Insert(L"shortcutBindings", SerializeShortcutSettings(settings.shortcutBindings));
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

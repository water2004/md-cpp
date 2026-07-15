#include "pch.h"
#include "settings/AppSettings.h"

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

namespace winrt::ElMd
{
    std::filesystem::path AppDataDirectory()
    {
        std::wstring value(32768, L'\0');
        auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
        if (length > 0 && length < value.size())
        {
            value.resize(length);
            return std::filesystem::path(value) / L"el-md";
        }
        return std::filesystem::temp_directory_path() / L"el-md";
    }

    SettingsLoadResult LoadAppSettings()
    {
        auto path = AppDataDirectory() / L"settings.json";
        if (!std::filesystem::exists(path)) return {};
        try
        {
            auto root = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(ReadUtf8(path)));
            if (root.GetNamedNumber(L"schemaVersion") != 1.0)
                throw std::runtime_error("unsupported settings schema version");
            AppSettings settings;
            settings.mathRenderingEnabled = root.GetNamedBoolean(L"mathRenderingEnabled");
            settings.themeId = winrt::to_string(root.GetNamedString(L"themeId"));
            if (settings.themeId.empty()) throw std::runtime_error("themeId cannot be empty");
            return { std::move(settings), true, {} };
        }
        catch (winrt::hresult_error const& error)
        {
            return { {}, false, L"Settings fallback: " + error.message() };
        }
        catch (std::exception const& error)
        {
            return { {}, false, L"Settings fallback: " + winrt::to_hstring(error.what()) };
        }
    }

    std::optional<winrt::hstring> SaveAppSettings(AppSettings const& settings)
    {
        try
        {
            auto directory = AppDataDirectory();
            std::filesystem::create_directories(directory);
            Windows::Data::Json::JsonObject root;
            root.Insert(L"schemaVersion", Windows::Data::Json::JsonValue::CreateNumberValue(1));
            root.Insert(L"mathRenderingEnabled", Windows::Data::Json::JsonValue::CreateBooleanValue(settings.mathRenderingEnabled));
            root.Insert(L"themeId", Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(settings.themeId)));
            auto target = directory / L"settings.json";
            auto temporary = directory / L"settings.json.tmp";
            WriteUtf8(temporary, winrt::to_string(root.Stringify()));
            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                winrt::throw_last_error();
            return std::nullopt;
        }
        catch (winrt::hresult_error const& error)
        {
            return L"Unable to save settings: " + error.message();
        }
        catch (std::exception const& error)
        {
            return L"Unable to save settings: " + winrt::to_hstring(error.what());
        }
    }
}

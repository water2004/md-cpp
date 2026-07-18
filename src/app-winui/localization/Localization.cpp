#include "pch.h"
#include "localization/Localization.h"

namespace winrt::Folia
{
    hstring Localize(std::wstring_view resourceId)
    {
        try
        {
            thread_local Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            auto value = loader.GetString(hstring(resourceId));
            return value.empty() ? hstring(resourceId) : value;
        }
        catch (...)
        {
            return hstring(resourceId);
        }
    }

    hstring LocalizeFormat(
        std::wstring_view resourceId,
        std::initializer_list<hstring> arguments)
    {
        auto localized = Localize(resourceId);
        std::wstring result(localized.c_str(), localized.size());
        std::size_t index = 0;
        for (auto const& argument : arguments)
        {
            auto token = L"{" + std::to_wstring(index++) + L"}";
            std::size_t position = 0;
            while ((position = result.find(token, position)) != std::wstring::npos)
            {
                result.replace(position, token.size(), argument.c_str(), argument.size());
                position += argument.size();
            }
        }
        return hstring(result);
    }

    bool IsSupportedLanguage(std::string_view languageId)
    {
        return languageId == "system" || languageId == "en-US" || languageId == "zh-CN";
    }

    void ApplyLanguageOverride(std::string_view languageId)
    {
        if (!IsSupportedLanguage(languageId)) languageId = "system";
        // This app is currently unpackaged, so an override is not persisted.
        // An empty string is not a BCP-47 tag and the WinAppSDK setter rejects it.
        if (languageId == "system") return;
        Microsoft::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(
            to_hstring(languageId));
    }
}

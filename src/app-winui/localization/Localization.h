#pragma once

#include <initializer_list>
#include <string_view>

namespace winrt::ElMd
{
    winrt::hstring Localize(std::wstring_view resourceId);
    winrt::hstring LocalizeFormat(
        std::wstring_view resourceId,
        std::initializer_list<winrt::hstring> arguments);
    void ApplyLanguageOverride(std::string_view languageId);
    bool IsSupportedLanguage(std::string_view languageId);
}

#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "localization/Localization.h"
#include "settings/AppSettings.h"
#include <shellapi.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::Folia::implementation
{
    namespace
    {
        std::optional<winrt::hstring> InitialDocumentPath()
        {
            struct LocalArguments
            {
                wchar_t** value{};
                ~LocalArguments() { if (value) ::LocalFree(value); }
            };
            int argumentCount = 0;
            LocalArguments arguments{ ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount) };
            if (!arguments.value) return std::nullopt;
            if (argumentCount < 2 || !arguments.value[1] || arguments.value[1][0] == L'\0')
                return std::nullopt;
            return winrt::hstring{ arguments.value[1] };
        }
    }

    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        ApplyLanguageOverride(LoadAppSettings().settings.languageId);
        auto initialDocument = InitialDocumentPath();
        window = initialDocument
            ? make<MainWindow>(*initialDocument)
            : make<MainWindow>();
        window.Activate();
    }
}

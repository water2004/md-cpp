#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "localization/Localization.h"
#include "settings/AppSettings.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::Folia::implementation
{
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
        window = make<MainWindow>();
        window.Activate();
    }
}

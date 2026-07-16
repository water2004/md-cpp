#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

import elmd.core.command;
import elmd.core.utf;

namespace winrt::ElMd::implementation
{
    namespace
    {
        Windows::UI::Color UiColor(elmd::Color color)
        {
            return Windows::UI::Color{ color.a, color.r, color.g, color.b };
        }

        Microsoft::UI::Xaml::Media::SolidColorBrush Brush(elmd::Color color)
        {
            return Microsoft::UI::Xaml::Media::SolidColorBrush(UiColor(color));
        }

        void UpdateBrushResource(
            Microsoft::UI::Xaml::ResourceDictionary const& resources,
            wchar_t const* name,
            elmd::Color color)
        {
            auto key = winrt::box_value(name);
            if (resources.HasKey(key))
            {
                if (auto brush = resources.Lookup(key)
                    .try_as<Microsoft::UI::Xaml::Media::SolidColorBrush>())
                {
                    brush.Color(UiColor(color));
                    return;
                }
            }
            resources.Insert(key, Brush(color));
        }

        Windows::Foundation::IReference<Windows::UI::Color> ColorReference(elmd::Color color)
        {
            return winrt::box_value(UiColor(color))
                .as<Windows::Foundation::IReference<Windows::UI::Color>>();
        }

        Microsoft::UI::Xaml::Media::FontFamily Font(std::string const& family)
        {
            return Microsoft::UI::Xaml::Media::FontFamily(winrt::to_hstring(family));
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        auto loadedSettings = winrt::ElMd::LoadAppSettings();
        appSettings = std::move(loadedSettings.settings);
        themeCatalog->Refresh();
        editorRenderer.SetMathRenderingEnabled(appSettings.mathRenderingEnabled);
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        RegisterCommandHandlers();
        InitializeTextInput();
        AttachControllers();
        RegisterWindowHandlers();
        UpdateSourceModeUi();
        UpdateDocumentInfo();
        if (!loadedSettings.diagnostic.empty()) SetStatus(loadedSettings.diagnostic);
        else if (!themeCatalog->Diagnostics().empty()) SetStatus(themeCatalog->Diagnostics().front());
    }

    void MainWindow::UpdateDocumentInfo()
    {
        auto const displayName = editorSession.DisplayName();
        auto const detail = editorSession.HasFile() ? editorSession.Path() : displayName;
        TitleDocumentText().Text(displayName);
        DocumentNameText().Text(displayName);
        Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(TitleDocumentText(), winrt::box_value(detail));
        Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(DocumentNameText(), winrt::box_value(detail));
        CharacterCountText().Text(winrt::to_hstring(editorSession.CharacterCount()) + L" characters");
        RevisionText().Text(L"Revision " + winrt::to_hstring(editorSession.Revision()));
    }

    void MainWindow::UpdateSourceModeUi()
    {
        auto source = editorSession.IsSourceMode();
        SourceModeButton().IsChecked(source);
        BoldButton().IsEnabled(!source);
        ItalicButton().IsEnabled(!source);
        StrikeButton().IsEnabled(!source);
        InlineCodeButton().IsEnabled(!source);
        BlocksButton().IsEnabled(!source);
        InsertButton().IsEnabled(!source);
    }

    void MainWindow::ToggleSourceMode()
    {
        if (!editorSession.ToggleSourceMode())
        {
            UpdateSourceModeUi();
            return;
        }
        keyboardController.ResetCaretGoal();
        editorRenderer.ResetDocumentCaches();
        UpdateSourceModeUi();
        UpdateDocumentInfo();
        SetStatus(editorSession.IsSourceMode() ? L"Source mode" : L"Rendered mode");
        sidebarController.Refresh();
        textInputController.NotifyTextChanged();
        RenderEditorSurface();
        if (editorRenderer.ScrollToPosition(editorSession.Selection().active)) RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::InitializeTextInput()
    {
        textInputController.Attach(
            editorSession,
            editorRenderer,
            EditorSurface(),
            [this](elmd::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { RenderEditorSurface(); },
            [this] { return WindowHandle(); });

    }

    bool MainWindow::ExecuteEditorCommand(elmd::Command const& command)
    {
        keyboardController.ResetCaretGoal();
        auto oldRevision = editorSession.Revision();
        if (!editorSession.ExecuteCommand(command))
        {
            return false;
        }

        if (editorSession.Revision() != oldRevision) UpdateDocumentInfo();
        sidebarController.Refresh();
        RenderEditorSurface();
        if (editorRenderer.ScrollToPosition(editorSession.Selection().active)) RenderEditorSurface();
        if (editorSession.Revision() != oldRevision) textInputController.NotifyTextChanged();
        else textInputController.NotifySelectionChanged();
        return true;
    }

    void MainWindow::NavigateToFootnote(elmd::TextPosition position)
    {
        editorSession.SetSelection(position, position);
        textInputController.NotifySelectionChanged();
        editorRenderer.ScrollToPosition(position);
        RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::ShowFootnoteFlyout(
        winrt::ElMd::EditorSurfaceRenderer::FootnoteHit const& hit,
        Windows::Foundation::Point position)
    {
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        using namespace Microsoft::UI::Xaml::Controls::Primitives;

        if (footnoteFlyout) footnoteFlyout.Hide();
        footnoteFlyout = Flyout();

        StackPanel panel;
        panel.Spacing(themeProfile.layout.footnote_preview_spacing);
        panel.MaxWidth(themeProfile.layout.footnote_preview_width);

        TextBlock heading;
        heading.Text(L"[^" + winrt::to_hstring(hit.label) + L"]");
        heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        panel.Children().Append(heading);

        const auto isReference = hit.kind == winrt::ElMd::EditorFootnoteControlKind::Reference;
        auto target = isReference
            ? editorSession.FootnoteDefinitionTarget(hit.label)
            : editorSession.FirstFootnoteReferenceTarget(hit.label);
        TextBlock preview;
        preview.TextWrapping(TextWrapping::Wrap);
        preview.IsTextSelectionEnabled(false);
        if (isReference)
        {
            auto text = editorSession.FootnotePreview(hit.label);
            preview.Text(target
                ? text.empty() ? L"Empty footnote definition." : winrt::to_hstring(text)
                : L"This reference has no definition.");
        }
        else
        {
            preview.Text(target ? L"Return to the first reference." : L"This definition has no reference.");
        }
        panel.Children().Append(preview);

        Button action;
        if (target)
        {
            action.Content(winrt::box_value(isReference ? L"Go to definition" : L"Back to reference"));
            action.Click([this, target = *target](auto const&, auto const&)
            {
                if (footnoteFlyout) footnoteFlyout.Hide();
                NavigateToFootnote(target);
            });
        }
        else if (isReference)
        {
            action.Content(winrt::box_value(L"Create definition"));
            action.Click([this, label = hit.label](auto const&, auto const&)
            {
                if (footnoteFlyout) footnoteFlyout.Hide();
                elmd::Command command;
                command.kind = elmd::CommandKind::CreateFootnoteDefinition;
                command.footnote_label = label;
                ExecuteEditorCommand(command);
            });
        }
        if (target || isReference) panel.Children().Append(action);

        footnoteFlyout.Content(panel);
        FlyoutShowOptions options;
        options.Position(winrt::box_value(position).as<Windows::Foundation::IReference<Windows::Foundation::Point>>());
        options.ShowMode(FlyoutShowMode::Standard);
        footnoteFlyout.ShowAt(EditorSurface(), options);
    }

    void MainWindow::HandlePointerWheel(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto delta = args.GetCurrentPoint(EditorSurface()).Properties().MouseWheelDelta();
        auto scrollDelta = static_cast<float>(-delta);
        if (args.Pointer().PointerDeviceType() == Microsoft::UI::Input::PointerDeviceType::Touchpad)
            scrollController.ScrollPreciselyBy(scrollDelta);
        else
            scrollController.QueueScrollBy(scrollDelta);
        args.Handled(true);
    }

    void MainWindow::SetStatus(winrt::hstring const& text)
    {
        lastCommand = text;
        StatusText().Text(text);
    }

    HWND MainWindow::WindowHandle()
    {
        HWND hwnd{};
        auto windowNative = get_strong().as<IWindowNative>();
        winrt::check_hresult(windowNative->get_WindowHandle(&hwnd));
        return hwnd;
    }

    void MainWindow::InitializeEditorSurface()
    {
        UpdateTheme();
        editorRenderer.SetInvalidateCallback([this] { RenderEditorSurface(); });
        editorRenderer.Initialize(EditorSurface());
        scrollController.Attach(editorRenderer, EditorScrollBar(), EditorScrollBarColumn(), WindowHandle(), [this] { RenderEditorSurface(); });
        RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::ResizeEditorSurface(double width, double height)
    {
        try
        {
            editorRenderer.Resize(EditorSurface(), width, height);
            RenderEditorSurface();
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Resize failed: " + error.message());
        }
    }

    void MainWindow::RenderEditorSurface()
    {
        if (settingsMode) return;
        try
        {
            editorRenderer.Render(editorSession.RenderFrame());
            scrollController.Sync();
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Render failed: " + error.message());
        }
    }

    elmd::Theme MainWindow::CurrentThemeVariant()
    {
        if (Windows::UI::ViewManagement::AccessibilitySettings().HighContrast())
            return elmd::Theme::HighContrast;
        auto background = Windows::UI::ViewManagement::UISettings().GetColorValue(
            Windows::UI::ViewManagement::UIColorType::Background);
        auto luminance = 0.2126 * background.R + 0.7152 * background.G + 0.0722 * background.B;
        return luminance < 128.0 ? elmd::Theme::Dark : elmd::Theme::Light;
    }

    void MainWindow::ApplyShellTheme()
    {
        auto const& colors = themeProfile.colors;
        auto resources = Root().Resources();
        // ThemeResource bindings retain the object returned by the dictionary. Mutating
        // stable brushes updates every existing binding; replacing them leaves controls
        // holding colors from the previous theme until another resource pass occurs.
        UpdateBrushResource(resources, L"ElMdShellBackgroundBrush", colors.shell_bg);
        UpdateBrushResource(resources, L"ElMdShellLayerBrush", colors.shell_layer_bg);
        UpdateBrushResource(resources, L"ElMdShellBorderBrush", colors.shell_border);
        UpdateBrushResource(resources, L"ElMdShellForegroundBrush", colors.shell_fg);
        UpdateBrushResource(resources, L"ElMdShellMutedForegroundBrush", colors.shell_muted_fg);
        UpdateBrushResource(resources, L"ElMdShellAccentBrush", colors.shell_accent);
        resources.Insert(winrt::box_value(L"ElMdUiFontFamily"), Font(themeProfile.typography.ui.family));
        resources.Insert(winrt::box_value(L"ElMdUiFontSize"), winrt::box_value(static_cast<double>(themeProfile.typography.ui.size)));
        resources.Insert(winrt::box_value(L"ElMdMonoFontFamily"), Font(themeProfile.typography.ui_monospace.family));

        Root().Background(Brush(colors.shell_bg));
        AppTitleBar().Background(Brush(colors.shell_bg));
        ShellCommandBar().Background(Brush(colors.shell_layer_bg));
        ShellCommandBar().BorderBrush(Brush(colors.shell_border));
        StatusBar().Background(Brush(colors.shell_layer_bg));
        StatusBar().BorderBrush(Brush(colors.shell_border));
        EditorScrollBar().Background(Brush(colors.shell_layer_bg));

        auto titleBar = AppWindow().TitleBar();
        titleBar.BackgroundColor(ColorReference(colors.shell_bg));
        titleBar.ForegroundColor(ColorReference(colors.shell_fg));
        titleBar.ButtonBackgroundColor(ColorReference(colors.shell_bg));
        titleBar.ButtonForegroundColor(ColorReference(colors.shell_fg));
        titleBar.ButtonHoverBackgroundColor(ColorReference(colors.shell_layer_bg));
        titleBar.ButtonHoverForegroundColor(ColorReference(colors.shell_fg));
        titleBar.ButtonPressedBackgroundColor(ColorReference(colors.shell_border));
        titleBar.ButtonPressedForegroundColor(ColorReference(colors.shell_fg));
        titleBar.ButtonInactiveBackgroundColor(ColorReference(colors.shell_bg));
        titleBar.ButtonInactiveForegroundColor(ColorReference(colors.shell_muted_fg));

        Root().RowDefinitions().GetAt(0).Height(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(themeProfile.layout.title_bar_height));
        DocumentNavigation().OpenPaneLength(themeProfile.layout.navigation_open_width);
        EditorScrollBar().Width(themeProfile.layout.scrollbar_width);
        EditorScrollBarColumn().Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(themeProfile.layout.scrollbar_width));
        scrollController.SetWidth(themeProfile.layout.scrollbar_width);
        StatusBar().MinHeight(themeProfile.layout.status_bar_min_height);
        SourceModeButton().FontFamily(Font(themeProfile.typography.ui_monospace.family));
    }

    void MainWindow::UpdateTheme()
    {
        if (updatingTheme) return;
        updatingTheme = true;
        struct ResetUpdating
        {
            bool& value;
            ~ResetUpdating() { value = false; }
        } resetUpdating{ updatingTheme };

        themeCatalog->Refresh();
        auto systemVariant = CurrentThemeVariant();
        auto loaded = themeCatalog->Resolve(appSettings.themeId, systemVariant);
        themeProfile = std::move(loaded.profile);
        ApplyShellTheme();
        using Microsoft::UI::Xaml::ElementTheme;
        if (appSettings.themeId == "system" || themeProfile.variant == elmd::Theme::HighContrast)
            Root().RequestedTheme(ElementTheme::Default);
        else
            Root().RequestedTheme(themeProfile.variant == elmd::Theme::Dark
                ? ElementTheme::Dark
                : ElementTheme::Light);
        editorSession.SetTheme(themeProfile);
        editorRenderer.SetTheme(themeProfile);
        if (settingsView)
        {
            settingsView->SetSystemVariant(systemVariant);
            settingsView->SetNavigationWidth(themeProfile.layout.navigation_open_width);
        }
        if (!loaded.diagnostic.empty()) SetStatus(loaded.diagnostic);
        RenderEditorSurface();
    }

}

#include "pch.h"
#include "MainWindow.xaml.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

import folia.core.command;
import folia.core.utf;

namespace winrt::Folia::implementation
{
    namespace
    {
        Windows::UI::Color UiColor(folia::Color color)
        {
            return Windows::UI::Color{ color.a, color.r, color.g, color.b };
        }

        Microsoft::UI::Xaml::Media::SolidColorBrush Brush(folia::Color color)
        {
            return Microsoft::UI::Xaml::Media::SolidColorBrush(UiColor(color));
        }

        void UpdateBrushResource(
            Microsoft::UI::Xaml::ResourceDictionary const& resources,
            wchar_t const* name,
            folia::Color color)
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

        Windows::Foundation::IReference<Windows::UI::Color> ColorReference(folia::Color color)
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
        LocalizeShell();
        auto iconPath = winrt::Folia::AssetPath(std::filesystem::path(L"branding") / L"Folia.ico");
        if (std::filesystem::exists(iconPath)) AppWindow().SetIcon(winrt::hstring(iconPath.c_str()));
        auto loadedSettings = winrt::Folia::LoadAppSettings();
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
        else if (!latexCommandCatalog->Diagnostics().empty())
            SetStatus(latexCommandCatalog->Diagnostics().front());
    }

    void MainWindow::LocalizeShell()
    {
        using Microsoft::UI::Xaml::Controls::ToolTipService;
        auto tooltip = [](auto const& element, wchar_t const* resource)
        {
            ToolTipService::SetToolTip(element, winrt::box_value(Localize(resource)));
        };

        ApplicationNameText().Text(Localize(L"AppName"));
        SidebarToggleButton().Label(Localize(L"Sidebar"));
        tooltip(SidebarToggleButton(), L"ToggleOutline");
        OpenButton().Label(Localize(L"Open"));
        tooltip(OpenButton(), L"OpenTooltip");
        SaveButton().Label(Localize(L"Save"));
        tooltip(SaveButton(), L"SaveTooltip");
        ExportPdfButton().Label(Localize(L"Pdf"));
        tooltip(ExportPdfButton(), L"PdfTooltip");
        BoldButton().Label(Localize(L"Bold"));
        tooltip(BoldButton(), L"Bold");
        ItalicButton().Label(Localize(L"Italic"));
        tooltip(ItalicButton(), L"Italic");
        StrikeButton().Label(Localize(L"Strike"));
        tooltip(StrikeButton(), L"Strikethrough");
        InlineCodeButton().Label(Localize(L"InlineCode"));
        tooltip(InlineCodeButton(), L"InlineCode");
        BlocksButton().Label(Localize(L"Blocks"));
        tooltip(BlocksButton(), L"BlockStructure");
        HeadingMenuItem().Text(Localize(L"Heading"));
        Heading1Button().Text(Localize(L"Heading1"));
        Heading2Button().Text(Localize(L"Heading2"));
        QuoteButton().Text(Localize(L"Quote"));
        ListMenuItem().Text(Localize(L"List"));
        UnorderedListButton().Text(Localize(L"BulletedList"));
        OrderedListButton().Text(Localize(L"NumberedList"));
        TaskListButton().Text(Localize(L"TaskList"));
        CodeBlockButton().Text(Localize(L"CodeBlock"));
        BlockMathButton().Text(Localize(L"MathBlock"));
        CalloutMenuItem().Text(Localize(L"Callout"));
        CalloutNoteMenuItem().Text(Localize(L"Note"));
        CalloutTipMenuItem().Text(Localize(L"Tip"));
        CalloutWarningMenuItem().Text(Localize(L"Warning"));
        InsertButton().Label(Localize(L"Insert"));
        tooltip(InsertButton(), L"InsertContent");
        TableButton().Text(Localize(L"Table"));
        InlineMathButton().Text(Localize(L"InlineMath"));
        LinkButton().Text(Localize(L"Link"));
        ImageButton().Text(Localize(L"Image"));
        FootnoteButton().Text(Localize(L"Footnote"));
        TocButton().Text(Localize(L"TableOfContents"));
        SettingsButton().Label(Localize(L"Settings"));
        tooltip(SettingsButton(), L"Settings");
        OutlineHeadingText().Text(Localize(L"Outline"));
        CutMenuItem().Text(Localize(L"Cut"));
        CopyMenuItem().Text(Localize(L"Copy"));
        PasteMenuItem().Text(Localize(L"Paste"));
        SelectAllMenuItem().Text(Localize(L"SelectAll"));
        tooltip(CancelOperationButton(), L"CancelOperation");
        tooltip(SourceModeButton(), L"SourceMode");
        FindQueryBox().PlaceholderText(Localize(L"Find"));
        ReplaceTextBox().PlaceholderText(Localize(L"Replace"));
        ReplaceCurrentButton().Content(winrt::box_value(Localize(L"Replace")));
        ReplaceAllButton().Content(winrt::box_value(Localize(L"ReplaceAll")));
        tooltip(FindRegexButton(), L"RegularExpression");
        tooltip(FindCaseButton(), L"MatchCase");
        tooltip(FindPreviousButton(), L"PreviousMatch");
        tooltip(FindNextButton(), L"NextMatch");
        tooltip(CloseFindButton(), L"Close");
        LatexSuggestionHint().Text(Localize(L"LatexSuggestionHint"));
        SetStatus(Localize(L"Ready"));
    }

    winrt::hstring MainWindow::LocalizedDocumentName()
    {
        return editorSession.HasFile() ? editorSession.DisplayName() : Localize(L"UntitledMarkdown");
    }

    void MainWindow::UpdateDocumentInfo()
    {
        auto const displayName = LocalizedDocumentName();
        auto const detail = editorSession.HasFile() ? editorSession.Path() : displayName;
        TitleDocumentText().Text(displayName);
        DocumentNameText().Text(displayName);
        Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(TitleDocumentText(), winrt::box_value(detail));
        Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(DocumentNameText(), winrt::box_value(detail));
        CharacterCountText().Text(LocalizeFormat(
            L"CharacterCount", { winrt::to_hstring(editorSession.CharacterCount()) }));
        RevisionText().Text(LocalizeFormat(
            L"Revision", { winrt::to_hstring(editorSession.Revision()) }));
        if (!settingsMode) Title(LocalizeFormat(L"WindowTitleDocument", { displayName }));
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
        SetStatus(Localize(editorSession.IsSourceMode() ? L"SourceMode" : L"RenderedMode"));
        sidebarController.Refresh();
        textInputController.NotifyTextChanged();
        if (FindReplaceBar().Visibility() == Microsoft::UI::Xaml::Visibility::Visible)
            RefreshSearch(true);
        else
            RenderEditorSurface();
        if (editorRenderer.ScrollToPosition(editorSession.Selection().active)) RenderEditorSurface();
        if (FindReplaceBar().Visibility() != Microsoft::UI::Xaml::Visibility::Visible)
            EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::InitializeTextInput()
    {
        textInputController.Attach(
            editorSession,
            editorRenderer,
            EditorSurface(),
            [this](folia::Command const& command) { return ExecuteEditorCommand(command); },
            [this] { RenderEditorSurface(); },
            [this] { return WindowHandle(); });

    }

    bool MainWindow::ExecuteEditorCommand(folia::Command const& command)
    {
        keyboardController.ResetCaretGoal();
        auto oldRevision = editorSession.Revision();
        if (!editorSession.ExecuteCommand(command))
        {
            return false;
        }

        if (editorSession.Revision() != oldRevision) UpdateDocumentInfo();
        sidebarController.Refresh();
        if (editorSession.Revision() != oldRevision
            && FindReplaceBar().Visibility() == Microsoft::UI::Xaml::Visibility::Visible)
            RefreshSearch(true);
        else
            RenderEditorSurface();
        if (editorRenderer.ScrollToPosition(editorSession.Selection().active)) RenderEditorSurface();
        if (editorSession.Revision() != oldRevision) textInputController.NotifyTextChanged();
        else textInputController.NotifySelectionChanged();
        return true;
    }

    void MainWindow::NavigateToFootnote(folia::TextPosition position)
    {
        editorSession.SetSelection(position, position);
        textInputController.NotifySelectionChanged();
        editorRenderer.ScrollToPosition(position);
        RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::ShowFootnoteFlyout(
        folia::platform::editor::EditorVisualFootnoteHit const& hit,
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

        const auto isReference = hit.kind == winrt::Folia::EditorFootnoteControlKind::Reference;
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
                ? text.empty() ? Localize(L"EmptyFootnoteDefinition") : winrt::to_hstring(text)
                : Localize(L"ReferenceHasNoDefinition"));
        }
        else
        {
            preview.Text(target ? Localize(L"ReturnToFirstReference") : Localize(L"DefinitionHasNoReference"));
        }
        panel.Children().Append(preview);

        Button action;
        if (target)
        {
            action.Content(winrt::box_value(Localize(isReference ? L"GoToDefinition" : L"BackToReference")));
            action.Click([this, target = *target](auto const&, auto const&)
            {
                if (footnoteFlyout) footnoteFlyout.Hide();
                NavigateToFootnote(target);
            });
        }
        else if (isReference)
        {
            action.Content(winrt::box_value(Localize(L"CreateDefinition")));
            action.Click([this, label = hit.label](auto const&, auto const&)
            {
                if (footnoteFlyout) footnoteFlyout.Hide();
                folia::Command command;
                command.kind = folia::CommandKind::CreateFootnoteDefinition;
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
        // Wheel input can arrive much faster than the presentation cadence.
        // Clearing an already inactive suggestion ListView on every pulse
        // needlessly invalidates the XAML tree and makes a continuous wheel
        // gesture compete with the editor frame scheduler.
        if (latexCompletionController.Active()) latexCompletionController.Cancel();
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

    void MainWindow::SetOperationProgress(
        bool active,
        std::optional<double> value,
        bool cancellable)
    {
        OperationProgressBar().Visibility(active
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
        OperationProgressBar().IsIndeterminate(active && !value.has_value());
        if (value) OperationProgressBar().Value(
            (std::clamp)(*value, 0.0, 1.0) * 100.0);
        CancelOperationButton().Visibility(active && cancellable
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
        OpenButton().IsEnabled(!active);
        ExportPdfButton().IsEnabled(!active);
        SettingsButton().IsEnabled(!active);
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
        editorRenderer.Initialize(EditorSurface());
        scrollController.Attach(editorRenderer, EditorScrollBar(), EditorScrollBarColumn(), WindowHandle(), [this] { RenderEditorSurface(); });
        editorRenderer.SetInvalidateCallback([this] { RenderEditorSurface(); });
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
            SetStatus(LocalizeFormat(L"ResizeFailed", { error.message() }));
        }
    }

    void MainWindow::RenderEditorSurface()
    {
        if (settingsMode) return;
        try
        {
            auto snippetPlaceholders = keyboardController.SnippetPlaceholders();
            auto frame = editorSession.RenderFrame();
            frame.snippetPlaceholders = snippetPlaceholders;
            editorRenderer.Render(frame);
            scrollController.Sync();
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(LocalizeFormat(L"RenderFailed", { error.message() }));
        }
    }

    folia::Theme MainWindow::CurrentThemeVariant()
    {
        if (Windows::UI::ViewManagement::AccessibilitySettings().HighContrast())
            return folia::Theme::HighContrast;
        auto background = Windows::UI::ViewManagement::UISettings().GetColorValue(
            Windows::UI::ViewManagement::UIColorType::Background);
        auto luminance = 0.2126 * background.R + 0.7152 * background.G + 0.0722 * background.B;
        return luminance < 128.0 ? folia::Theme::Dark : folia::Theme::Light;
    }

    void MainWindow::ApplyShellTheme()
    {
        auto const& colors = themeProfile.colors;
        auto resources = Root().Resources();
        // ThemeResource bindings retain the object returned by the dictionary. Mutating
        // stable brushes updates every existing binding; replacing them leaves controls
        // holding colors from the previous theme until another resource pass occurs.
        UpdateBrushResource(resources, L"FoliaShellBackgroundBrush", colors.shell_bg);
        UpdateBrushResource(resources, L"FoliaShellLayerBrush", colors.shell_layer_bg);
        UpdateBrushResource(resources, L"FoliaShellBorderBrush", colors.shell_border);
        UpdateBrushResource(resources, L"FoliaShellForegroundBrush", colors.shell_fg);
        UpdateBrushResource(resources, L"FoliaShellMutedForegroundBrush", colors.shell_muted_fg);
        UpdateBrushResource(resources, L"FoliaShellAccentBrush", colors.shell_accent);
        resources.Insert(winrt::box_value(L"FoliaUiFontFamily"), Font(themeProfile.typography.ui.family));
        resources.Insert(winrt::box_value(L"FoliaUiFontSize"), winrt::box_value(static_cast<double>(themeProfile.typography.ui.size)));
        resources.Insert(winrt::box_value(L"FoliaMonoFontFamily"), Font(themeProfile.typography.ui_monospace.family));

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
        if (appSettings.themeId == "system" || themeProfile.variant == folia::Theme::HighContrast)
            Root().RequestedTheme(ElementTheme::Default);
        else
            Root().RequestedTheme(themeProfile.variant == folia::Theme::Dark
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

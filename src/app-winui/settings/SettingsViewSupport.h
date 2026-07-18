#pragma once

namespace winrt::Folia::settings_ui
{
    namespace Xaml = Microsoft::UI::Xaml;
    namespace Controls = Microsoft::UI::Xaml::Controls;

    inline constexpr std::array<std::string_view, 3> LanguageIds{
        "system", "en-US", "zh-CN"};

    inline std::int32_t LanguageIndex(std::string_view languageId)
    {
        auto found = std::ranges::find(LanguageIds, languageId);
        return found == LanguageIds.end()
            ? 0
            : static_cast<std::int32_t>(std::distance(LanguageIds.begin(), found));
    }

    inline Xaml::Media::SolidColorBrush Brush(folia::Color color)
    {
        return Xaml::Media::SolidColorBrush(
            winrt::Windows::UI::Color{color.a, color.r, color.g, color.b});
    }

    inline Xaml::Media::FontFamily Font(std::string const& family)
    {
        return Xaml::Media::FontFamily(winrt::to_hstring(family));
    }

    inline Controls::TextBlock Text(winrt::hstring const& value, double size = 14.0)
    {
        Controls::TextBlock text;
        text.Text(value);
        text.FontSize(size);
        text.TextWrapping(Xaml::TextWrapping::Wrap);
        return text;
    }

    inline Controls::TextBlock PageHeading(winrt::hstring const& value)
    {
        auto text = Text(value, 24.0);
        text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        return text;
    }

    inline Controls::Border Card(Xaml::UIElement const& child)
    {
        Controls::Border border;
        border.Padding(Xaml::Thickness{16, 14, 16, 14});
        border.CornerRadius(Xaml::CornerRadius{8});
        border.BorderThickness(Xaml::Thickness{1});
        border.BorderBrush(Brush({128, 128, 128, 72}));
        border.Child(child);
        return border;
    }

    inline winrt::Windows::Foundation::Uri FileUri(std::filesystem::path path)
    {
        path = std::filesystem::absolute(path);
        return winrt::Windows::Foundation::Uri(
            L"file:///" + winrt::hstring(path.generic_wstring()));
    }

    inline winrt::Windows::Foundation::Collections::IObservableVector<
        winrt::Windows::Foundation::IInspectable>
        ReadUtf8Lines(std::filesystem::path const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open text asset");
        std::string source(
            std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{});
        if (source.starts_with("\xEF\xBB\xBF")) source.erase(0, 3);

        auto lines = winrt::single_threaded_observable_vector<
            winrt::Windows::Foundation::IInspectable>();
        std::size_t start = 0;
        while (start <= source.size())
        {
            auto end = source.find('\n', start);
            if (end == std::string::npos) end = source.size();
            auto length = end - start;
            if (length != 0 && source[start + length - 1] == '\r') --length;
            lines.Append(winrt::box_value(winrt::to_hstring(
                std::string_view{source}.substr(start, length))));
            if (end == source.size()) break;
            start = end + 1;
        }
        return lines;
    }

    inline Controls::NavigationViewItem NavigationItem(
        winrt::hstring const& label,
        winrt::hstring const& tag,
        wchar_t const* glyph)
    {
        Controls::NavigationViewItem item;
        item.Content(winrt::box_value(label));
        item.Tag(winrt::box_value(tag));
        Controls::FontIcon icon;
        icon.Glyph(glyph);
        item.Icon(icon);
        return item;
    }

    inline Controls::StackPanel PagePanel()
    {
        Controls::StackPanel panel;
        panel.Spacing(16);
        panel.Margin(Xaml::Thickness{24, 18, 24, 24});
        return panel;
    }
}

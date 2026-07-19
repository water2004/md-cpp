#include "pch.h"
#include "MainWindow.xaml.h"
#include "localization/Localization.h"
#include "storage/DocumentEncodingService.h"

namespace winrt::Folia::implementation
{
    namespace
    {
        struct EncodingChoice
        {
            std::string name;
            DocumentByteOrderMark byteOrderMark = DocumentByteOrderMark::None;
            int confidence = 0;
            bool detected = false;
        };

        bool SameEncoding(std::string_view left, std::string_view right)
        {
            return _stricmp(std::string(left).c_str(), std::string(right).c_str()) == 0;
        }

        DocumentByteOrderMark DefaultSaveBom(std::string_view name)
        {
            if (SameEncoding(name, "UTF-16")) return DocumentByteOrderMark::Utf16LittleEndian;
            if (SameEncoding(name, "UTF-16LE")) return DocumentByteOrderMark::Utf16LittleEndian;
            if (SameEncoding(name, "UTF-16BE")) return DocumentByteOrderMark::Utf16BigEndian;
            if (SameEncoding(name, "UTF-32")) return DocumentByteOrderMark::Utf32LittleEndian;
            if (SameEncoding(name, "UTF-32LE")) return DocumentByteOrderMark::Utf32LittleEndian;
            if (SameEncoding(name, "UTF-32BE")) return DocumentByteOrderMark::Utf32BigEndian;
            return DocumentByteOrderMark::None;
        }

        std::vector<EncodingChoice> BuildChoices(
            EditorSession const& session,
            bool saveWithEncoding)
        {
            std::vector<EncodingChoice> result;
            auto append = [&](EncodingChoice choice)
            {
                auto duplicate = std::find_if(result.begin(), result.end(), [&](auto const& existing)
                {
                    return SameEncoding(existing.name, choice.name)
                        && existing.byteOrderMark == choice.byteOrderMark;
                });
                if (duplicate == result.end()) result.push_back(std::move(choice));
            };

            if (!saveWithEncoding)
            {
                for (auto const& candidate : session.EncodingCandidates())
                {
                    append({
                        candidate.name,
                        DocumentByteOrderMark::None,
                        candidate.confidence,
                        true,
                    });
                }
            }
            for (auto const& name : DocumentEncodingService::AvailableEncodings())
            {
                append({
                    name,
                    saveWithEncoding ? DefaultSaveBom(name) : DocumentByteOrderMark::None,
                    0,
                    false,
                });
                if (saveWithEncoding && SameEncoding(name, "UTF-8"))
                    append({ name, DocumentByteOrderMark::Utf8, 0, false });
            }
            return result;
        }

        winrt::hstring ChoiceLabel(EncodingChoice const& choice)
        {
            auto label = winrt::to_hstring(choice.name);
            if (choice.byteOrderMark == DocumentByteOrderMark::Utf8)
                label = label + L" BOM";
            if (choice.detected)
                label = label + L"  —  " + winrt::to_hstring(choice.confidence) + L"%";
            return label;
        }
    }

    winrt::fire_and_forget MainWindow::ShowEncodingPicker(bool saveWithEncoding)
    {
        try
        {
            if (!editorSession.HasFile()) co_return;
            auto dialog = Microsoft::UI::Xaml::Controls::ContentDialog();
            dialog.XamlRoot(Root().XamlRoot());
            dialog.Title(winrt::box_value(Localize(
                saveWithEncoding ? L"SaveWithEncoding" : L"ReopenWithEncoding")));
            dialog.PrimaryButtonText(Localize(saveWithEncoding ? L"Save" : L"Reopen"));
            dialog.CloseButtonText(Localize(L"Cancel"));
            dialog.DefaultButton(Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
            dialog.IsPrimaryButtonEnabled(false);

            auto root = Microsoft::UI::Xaml::Controls::Grid();
            root.MinWidth(460);
            root.RowDefinitions().Append(Microsoft::UI::Xaml::Controls::RowDefinition());
            root.RowDefinitions().Append(Microsoft::UI::Xaml::Controls::RowDefinition());
            root.RowDefinitions().GetAt(0).Height(Microsoft::UI::Xaml::GridLengthHelper::Auto());
            root.RowDefinitions().GetAt(1).Height(
                Microsoft::UI::Xaml::GridLengthHelper::FromPixels(420));

            auto search = Microsoft::UI::Xaml::Controls::TextBox();
            search.Margin({ 0, 0, 0, 8 });
            search.PlaceholderText(Localize(L"SearchEncodings"));
            root.Children().Append(search);

            auto list = Microsoft::UI::Xaml::Controls::ListView();
            list.SetValue(Microsoft::UI::Xaml::Controls::Grid::RowProperty(), winrt::box_value(1));
            list.SelectionMode(Microsoft::UI::Xaml::Controls::ListViewSelectionMode::Single);
            list.SingleSelectionFollowsFocus(false);
            root.Children().Append(list);
            dialog.Content(root);

            auto choices = std::make_shared<std::vector<EncodingChoice>>(
                BuildChoices(editorSession, saveWithEncoding));
            auto repopulate = [list, choices](std::wstring_view query)
            {
                std::wstring lowered(query);
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t value)
                {
                    return static_cast<wchar_t>(std::towlower(value));
                });
                list.Items().Clear();
                for (std::size_t index = 0; index < choices->size(); ++index)
                {
                    auto label = ChoiceLabel((*choices)[index]);
                    std::wstring candidate(label.c_str(), label.size());
                    std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](wchar_t value)
                    {
                        return static_cast<wchar_t>(std::towlower(value));
                    });
                    if (!lowered.empty() && candidate.find(lowered) == std::wstring::npos) continue;
                    auto item = Microsoft::UI::Xaml::Controls::ListViewItem();
                    item.Content(winrt::box_value(label));
                    item.Tag(winrt::box_value(static_cast<std::uint32_t>(index)));
                    list.Items().Append(item);
                }
            };
            repopulate({});

            auto const& current = editorSession.Encoding();
            for (std::uint32_t index = 0; index < list.Items().Size(); ++index)
            {
                auto item = list.Items().GetAt(index).as<Microsoft::UI::Xaml::Controls::ListViewItem>();
                auto choiceIndex = winrt::unbox_value<std::uint32_t>(item.Tag());
                auto const& choice = (*choices)[choiceIndex];
                if (!SameEncoding(choice.name, current.name)) continue;
                if (saveWithEncoding && choice.byteOrderMark != current.byteOrderMark) continue;
                list.SelectedIndex(static_cast<std::int32_t>(index));
                list.ScrollIntoView(item);
                break;
            }
            dialog.IsPrimaryButtonEnabled(list.SelectedItem() != nullptr);

            search.TextChanged([repopulate](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
            {
                auto text = sender.as<Microsoft::UI::Xaml::Controls::TextBox>().Text();
                repopulate(std::wstring_view(text.c_str(), text.size()));
            });
            list.SelectionChanged([dialog](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
            {
                auto view = sender.as<Microsoft::UI::Xaml::Controls::ListView>();
                dialog.IsPrimaryButtonEnabled(view.SelectedItem() != nullptr);
            });
            auto result = co_await dialog.ShowAsync();
            if (result != Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary) co_return;
            auto selected = list.SelectedItem();
            if (!selected) co_return;
            auto item = selected.as<Microsoft::UI::Xaml::Controls::ListViewItem>();
            auto choiceIndex = winrt::unbox_value<std::uint32_t>(item.Tag());
            auto const choice = (*choices)[choiceIndex];
            if (saveWithEncoding)
            {
                documentController.SaveDocumentWithEncoding({
                    choice.name,
                    choice.byteOrderMark,
                    100,
                    false,
                });
            }
            else
            {
                documentController.ReopenDocumentWithEncoding(choice.name);
            }
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(LocalizeFormat(
                saveWithEncoding ? L"StatusSaveFailed" : L"StatusOpenFailed",
                { error.message() }));
        }
        catch (std::exception const& error)
        {
            SetStatus(LocalizeFormat(
                saveWithEncoding ? L"StatusSaveFailed" : L"StatusOpenFailed",
                { winrt::to_hstring(error.what()) }));
        }
    }
}

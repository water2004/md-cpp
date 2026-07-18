#include "pch.h"
#include "editor/interaction/EditorLatexCompletionController.h"
#include "localization/Localization.h"

import folia.core.utf;

namespace winrt::Folia
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    void EditorLatexCompletionController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        SwapChainPanel const& editorSurface,
        Canvas const& overlay,
        Border const& popup,
        TextBlock const& prefixLabel,
        ListView const& list,
        std::shared_ptr<LatexCommandCatalog> catalog,
        InsertSnippet insertSnippet)
    {
        Detach();
        session_ = &session;
        renderer_ = &renderer;
        editorSurface_ = editorSurface;
        overlay_ = overlay;
        popup_ = popup;
        prefixLabel_ = prefixLabel;
        list_ = list;
        catalog_ = std::move(catalog);
        insertSnippet_ = std::move(insertSnippet);
        list_.IsItemClickEnabled(true);
        itemClickToken_ = list_.ItemClick([this](auto const&, ItemClickEventArgs const& args)
        {
            auto element = args.ClickedItem().try_as<FrameworkElement>();
            if (!element || !element.Tag()) return;
            auto id = winrt::to_string(unbox_value<hstring>(element.Tag()));
            auto candidates = completion_.Candidates();
            auto found = std::ranges::find(candidates, id,
                [](auto const& candidate) { return candidate.command.id; });
            if (found == candidates.end()) return;
            completion_.Select(static_cast<std::size_t>(found - candidates.begin()));
            AcceptSelected();
        });
        Cancel();
    }

    void EditorLatexCompletionController::Detach()
    {
        Cancel();
        if (list_ && itemClickToken_.value) list_.ItemClick(itemClickToken_);
        itemClickToken_ = {};
        session_ = nullptr;
        renderer_ = nullptr;
        editorSurface_ = nullptr;
        overlay_ = nullptr;
        popup_ = nullptr;
        prefixLabel_ = nullptr;
        list_ = nullptr;
        catalog_.reset();
        insertSnippet_ = {};
    }

    void EditorLatexCompletionController::SetEnabled(bool enabled)
    {
        enabled_ = enabled;
        if (!enabled_) Cancel();
    }

    void EditorLatexCompletionController::Refresh()
    {
        if (!enabled_ || !session_ || !renderer_ || !catalog_)
        {
            Cancel();
            return;
        }
        auto query = session_->LatexCommandPrefixAtCaret();
        if (!query)
        {
            Cancel();
            return;
        }
        auto candidates = catalog_->Query(query->prefix);
        auto selection = session_->Selection();
        if (!completion_.Update(
            selection.active.container_id, query->replacement, std::move(candidates)))
        {
            Cancel();
            return;
        }
        prefixLabel_.Text(L"\\" + winrt::to_hstring(folia::cps_to_utf8(query->prefix)));
        RefreshItems();
        UpdatePosition();
        overlay_.IsHitTestVisible(true);
        popup_.Visibility(Visibility::Visible);
    }

    bool EditorLatexCompletionController::HandleKey(winrt::Windows::System::VirtualKey key)
    {
        if (!completion_.Active()) return false;
        using winrt::Windows::System::VirtualKey;
        switch (key)
        {
            case VirtualKey::Up: completion_.Move(-1); break;
            case VirtualKey::Down: completion_.Move(1); break;
            case VirtualKey::Tab:
            case VirtualKey::Enter: return AcceptSelected();
            case VirtualKey::Escape: Cancel(); return true;
            default: return false;
        }
        list_.SelectedIndex(static_cast<std::int32_t>(completion_.SelectedIndex()));
        list_.ScrollIntoView(list_.SelectedItem());
        return true;
    }

    bool EditorLatexCompletionController::AcceptSelected()
    {
        if (!insertSnippet_ || !catalog_) return false;
        auto accepted = completion_.Accept();
        if (!accepted) return false;
        popup_.Visibility(Visibility::Collapsed);
        auto inserted = insertSnippet_(
            accepted->container, accepted->replacement, accepted->command.snippet);
        if (inserted) catalog_->RecordUsage(accepted->command.id);
        if (editorSurface_) editorSurface_.Focus(FocusState::Programmatic);
        return inserted;
    }

    void EditorLatexCompletionController::Cancel()
    {
        completion_.Cancel();
        if (popup_) popup_.Visibility(Visibility::Collapsed);
        if (overlay_) overlay_.IsHitTestVisible(false);
        if (list_) list_.Items().Clear();
    }

    void EditorLatexCompletionController::RefreshItems()
    {
        list_.Items().Clear();
        for (auto const& candidate : completion_.Candidates())
        {
            Grid row;
            row.Tag(box_value(winrt::to_hstring(candidate.command.id)));
            row.Padding(Thickness{12, 8, 12, 8});
            row.ColumnSpacing(12);
            ColumnDefinition commandColumn;
            commandColumn.Width(GridLengthHelper::FromPixels(142));
            row.ColumnDefinitions().Append(commandColumn);
            row.ColumnDefinitions().Append(ColumnDefinition{});

            StackPanel command;
            command.Spacing(2);
            TextBlock trigger;
            trigger.Text(L"\\" + winrt::to_hstring(
                folia::cps_to_utf8(candidate.command.trigger)));
            trigger.FontFamily(Media::FontFamily(L"Cascadia Mono"));
            trigger.FontSize(15);
            trigger.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            trigger.Foreground(Application::Current().Resources().Lookup(
                box_value(L"AccentTextFillColorPrimaryBrush")).as<Media::Brush>());
            command.Children().Append(trigger);
            TextBlock source;
            source.Text(Localize(candidate.command.built_in ? L"BuiltIn" : L"Custom"));
            source.FontSize(11);
            source.Opacity(0.68);
            command.Children().Append(source);
            row.Children().Append(command);

            StackPanel detail;
            detail.Spacing(2);
            TextBlock description;
            description.Text(CandidateDescription(candidate.command));
            description.FontSize(13);
            detail.Children().Append(description);
            TextBlock snippet;
            snippet.Text(winrt::to_hstring(folia::cps_to_utf8(candidate.command.snippet)));
            snippet.FontFamily(Media::FontFamily(L"Cascadia Mono"));
            snippet.FontSize(11);
            snippet.Opacity(0.62);
            snippet.TextTrimming(TextTrimming::CharacterEllipsis);
            snippet.MaxLines(1);
            detail.Children().Append(snippet);
            Grid::SetColumn(detail, 1);
            row.Children().Append(detail);
            list_.Items().Append(row);
        }
        list_.SelectedIndex(static_cast<std::int32_t>(completion_.SelectedIndex()));
    }

    void EditorLatexCompletionController::UpdatePosition()
    {
        if (!session_ || !renderer_ || !editorSurface_ || !overlay_ || !popup_) return;
        auto caret = renderer_->CaretBounds(session_->Selection().active);
        if (!caret) return;
        auto transform = editorSurface_.TransformToVisual(overlay_);
        auto below = transform.TransformPoint({caret->left, caret->bottom});
        auto above = transform.TransformPoint({caret->left, caret->top});
        constexpr double popupWidth = 430.0;
        auto estimatedHeight = (std::min)(330.0,
            58.0 + static_cast<double>(completion_.Candidates().size()) * 58.0);
        auto left = std::clamp(
            static_cast<double>(below.X), 8.0,
            (std::max)(8.0, overlay_.ActualWidth() - popupWidth - 8.0));
        auto top = static_cast<double>(below.Y) + 8.0;
        if (top + estimatedHeight > overlay_.ActualHeight() - 8.0)
            top = (std::max)(8.0, static_cast<double>(above.Y) - estimatedHeight - 8.0);
        Canvas::SetLeft(popup_, left);
        Canvas::SetTop(popup_, top);
    }

    hstring EditorLatexCompletionController::CandidateDescription(
        folia::LatexCommandDefinition const& command) const
    {
        if (!command.built_in) return winrt::to_hstring(command.description);
        auto key = winrt::to_hstring(command.description);
        return Localize(std::wstring_view{key.c_str(), key.size()});
    }
}

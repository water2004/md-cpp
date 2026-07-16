#include "pch.h"
#include "editor/interaction/EditorSidebarController.h"

namespace winrt::ElMd
{
    void EditorSidebarController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorTextInputController& textInput,
        winrt::Microsoft::UI::Xaml::Controls::ListView const& outline,
        Render render)
    {
        Detach();
        session_ = &session;
        renderer_ = &renderer;
        textInput_ = &textInput;
        outline_ = outline;
        render_ = std::move(render);
        Refresh();
    }

    void EditorSidebarController::Detach()
    {
        if (outline_) outline_.ItemsSource(nullptr);
        session_ = nullptr;
        renderer_ = nullptr;
        textInput_ = nullptr;
        outline_ = nullptr;
        outlineItems_ = nullptr;
        render_ = {};
        outlinePositions_.clear();
        outlineContentKey_.reset();
    }

    void EditorSidebarController::Refresh()
    {
        RefreshOutline();
    }

    void EditorSidebarController::SelectOutline(winrt::Windows::Foundation::IInspectable const& selectedItem)
    {
        if (!session_ || !renderer_ || !outline_ || !selectedItem) return;
        auto const selectedIndex = outline_.SelectedIndex();
        if (selectedIndex < 0 || static_cast<std::size_t>(selectedIndex) >= outlinePositions_.size()) return;
        auto const& position = outlinePositions_[static_cast<std::size_t>(selectedIndex)];
        session_->SetSelection(position, position);
        if (textInput_) textInput_->NotifySelectionChanged();
        renderer_->ScrollToPosition(position);
        if (render_) render_();
    }

    void EditorSidebarController::RefreshOutline()
    {
        if (!session_ || !outline_) return;
        auto const contentKey = session_->RenderModel().outline.content_key;
        if (outlineContentKey_ == contentKey) return;
        auto const flatItems = session_->RenderModel().outline.flat_items();
        std::vector<winrt::Windows::Foundation::IInspectable> labels;
        std::vector<elmd::TextPosition> positions;
        labels.reserve(flatItems.size());
        positions.reserve(flatItems.size());
        for (auto const* item : flatItems)
        {
            std::wstring indent((std::max)(0, static_cast<int>(item->level) - 1) * 2, L' ');
            auto title = winrt::to_hstring(item->title_plain_text);
            labels.push_back(winrt::box_value(winrt::hstring(indent + std::wstring(title.c_str()))));
            positions.push_back(item->position);
        }
        outlinePositions_ = std::move(positions);
        outlineContentKey_ = contentKey;
        outlineItems_ = winrt::single_threaded_observable_vector(std::move(labels));
        outline_.ItemsSource(outlineItems_);
    }

}

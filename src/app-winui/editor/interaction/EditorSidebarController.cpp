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
        session_ = nullptr;
        renderer_ = nullptr;
        textInput_ = nullptr;
        outline_ = nullptr;
        render_ = {};
        outlinePositions_.clear();
        outlineLabels_.clear();
        outlineContentKey_.reset();
    }

    void EditorSidebarController::Refresh()
    {
        RefreshOutline();
    }

    void EditorSidebarController::SelectOutline(winrt::Windows::Foundation::IInspectable const& selectedItem)
    {
        if (!session_ || !renderer_ || !outline_ || !selectedItem) return;
        auto selectedText = winrt::unbox_value<winrt::hstring>(selectedItem);
        for (uint32_t index = 0; index < outline_.Items().Size() && index < outlinePositions_.size(); ++index)
        {
            if (winrt::unbox_value<winrt::hstring>(outline_.Items().GetAt(index)) != selectedText) continue;
            session_->SetSelection(outlinePositions_[index], outlinePositions_[index]);
            if (textInput_) textInput_->NotifySelectionChanged();
            renderer_->ScrollToPosition(outlinePositions_[index]);
            if (render_) render_();
            return;
        }
    }

    void EditorSidebarController::RefreshOutline()
    {
        if (!session_ || !outline_) return;
        auto const contentKey = session_->RenderModel().outline.content_key;
        if (outlineContentKey_ == contentKey) return;
        std::vector<std::wstring> labels;
        std::vector<elmd::TextPosition> positions;
        for (auto const* item : session_->RenderModel().outline.flat_items())
        {
            std::wstring indent((std::max)(0, static_cast<int>(item->level) - 1) * 2, L' ');
            auto title = winrt::to_hstring(item->title_plain_text);
            labels.push_back(indent + std::wstring(title.c_str()));
            positions.push_back(item->position);
        }
        if (labels == outlineLabels_ && positions == outlinePositions_) return;
        outlineLabels_ = std::move(labels);
        outlinePositions_ = std::move(positions);
        outlineContentKey_ = contentKey;
        outline_.Items().Clear();
        for (auto const& label : outlineLabels_) outline_.Items().Append(winrt::box_value(winrt::hstring(label)));
    }

}

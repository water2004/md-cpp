#include "pch.h"
#include "EditorSidebarController.h"

namespace winrt::ElMd
{
    void EditorSidebarController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorTextInputController& textInput,
        winrt::Microsoft::UI::Xaml::Controls::ListView const& outline,
        winrt::Microsoft::UI::Xaml::Controls::ListView const& diagnostics,
        Render render)
    {
        Detach();
        session_ = &session;
        renderer_ = &renderer;
        textInput_ = &textInput;
        outline_ = outline;
        diagnostics_ = diagnostics;
        render_ = std::move(render);
        Refresh();
    }

    void EditorSidebarController::Detach()
    {
        session_ = nullptr;
        renderer_ = nullptr;
        textInput_ = nullptr;
        outline_ = nullptr;
        diagnostics_ = nullptr;
        render_ = {};
        outlinePositions_.clear();
        diagnosticPositions_.clear();
        outlineLabels_.clear();
        diagnosticLabels_.clear();
    }

    void EditorSidebarController::Refresh()
    {
        RefreshOutline();
        RefreshDiagnostics();
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

    void EditorSidebarController::SelectDiagnostic(winrt::Windows::Foundation::IInspectable const& selectedItem)
    {
        if (!session_ || !renderer_ || !diagnostics_ || !selectedItem) return;
        auto selectedText = winrt::unbox_value<winrt::hstring>(selectedItem);
        if (selectedText == L"No diagnostics") return;
        for (uint32_t index = 0; index < diagnostics_.Items().Size() && index < diagnosticPositions_.size(); ++index)
        {
            if (winrt::unbox_value<winrt::hstring>(diagnostics_.Items().GetAt(index)) != selectedText) continue;
            if (!diagnosticPositions_[index]) return;
            session_->SetSelection(*diagnosticPositions_[index], *diagnosticPositions_[index]);
            if (textInput_) textInput_->NotifySelectionChanged();
            renderer_->ScrollToPosition(*diagnosticPositions_[index]);
            if (render_) render_();
            return;
        }
    }

    void EditorSidebarController::RefreshOutline()
    {
        if (!session_ || !outline_) return;
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
        outline_.Items().Clear();
        for (auto const& label : outlineLabels_) outline_.Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    void EditorSidebarController::RefreshDiagnostics()
    {
        if (!session_ || !diagnostics_) return;
        std::vector<std::wstring> labels;
        std::vector<std::optional<elmd::TextPosition>> positions;
        for (auto const& diagnostic : session_->RenderModel().diagnostics)
        {
            auto severity = L"Warning";
            if (diagnostic.severity == elmd::RenderDiagnostic::Sev::Info) severity = L"Info";
            else if (diagnostic.severity == elmd::RenderDiagnostic::Sev::Error) severity = L"Error";
            auto label = winrt::hstring(severity) + L": " + winrt::to_hstring(diagnostic.message);
            labels.emplace_back(label.c_str());
            positions.push_back(diagnostic.source_span
                ? std::optional<elmd::TextPosition>{{diagnostic.source_span->container_id, diagnostic.source_span->source_range.start, elmd::TextAffinity::Downstream}}
                : std::nullopt);
        }
        if (labels.empty())
        {
            labels.push_back(L"No diagnostics");
            positions.push_back(std::nullopt);
        }
        if (labels == diagnosticLabels_ && positions == diagnosticPositions_) return;
        diagnosticLabels_ = std::move(labels);
        diagnosticPositions_ = std::move(positions);
        diagnostics_.Items().Clear();
        for (auto const& label : diagnosticLabels_) diagnostics_.Items().Append(winrt::box_value(winrt::hstring(label)));
    }
}

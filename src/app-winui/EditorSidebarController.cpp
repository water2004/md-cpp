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
        outlineOffsets_.clear();
        diagnosticOffsets_.clear();
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
        for (uint32_t index = 0; index < outline_.Items().Size() && index < outlineOffsets_.size(); ++index)
        {
            if (winrt::unbox_value<winrt::hstring>(outline_.Items().GetAt(index)) != selectedText) continue;
            session_->SetSelection(outlineOffsets_[index], outlineOffsets_[index]);
            if (textInput_) textInput_->NotifySelectionChanged();
            renderer_->ScrollToSourceOffset(outlineOffsets_[index]);
            if (render_) render_();
            return;
        }
    }

    void EditorSidebarController::SelectDiagnostic(winrt::Windows::Foundation::IInspectable const& selectedItem)
    {
        if (!session_ || !renderer_ || !diagnostics_ || !selectedItem) return;
        auto selectedText = winrt::unbox_value<winrt::hstring>(selectedItem);
        if (selectedText == L"No diagnostics") return;
        for (uint32_t index = 0; index < diagnostics_.Items().Size() && index < diagnosticOffsets_.size(); ++index)
        {
            if (winrt::unbox_value<winrt::hstring>(diagnostics_.Items().GetAt(index)) != selectedText) continue;
            session_->SetSelection(diagnosticOffsets_[index], diagnosticOffsets_[index]);
            if (textInput_) textInput_->NotifySelectionChanged();
            renderer_->ScrollToSourceOffset(diagnosticOffsets_[index]);
            if (render_) render_();
            return;
        }
    }

    void EditorSidebarController::RefreshOutline()
    {
        if (!session_ || !outline_) return;
        std::vector<std::size_t> headingOffsets;
        for (auto const& block : session_->Core().renderModel.blocks)
        {
            if (block.kind == elmd::RenderBlockKind::Text && block.block_style.margin_top >= 4.0f && block.content_range.end.v > block.content_range.start.v)
            {
                headingOffsets.push_back(block.content_range.start.v);
            }
        }
        std::vector<std::wstring> labels;
        std::vector<std::size_t> offsets;
        std::size_t headingIndex = 0;
        for (auto const* item : session_->Core().renderModel.outline.flat_items())
        {
            std::wstring indent((std::max)(0, static_cast<int>(item->level) - 1) * 2, L' ');
            auto title = winrt::to_hstring(item->title_plain_text);
            labels.push_back(indent + std::wstring(title.c_str()));
            offsets.push_back(headingIndex < headingOffsets.size() ? headingOffsets[headingIndex] : 0);
            ++headingIndex;
        }
        if (labels == outlineLabels_ && offsets == outlineOffsets_) return;
        outlineLabels_ = std::move(labels);
        outlineOffsets_ = std::move(offsets);
        outline_.Items().Clear();
        for (auto const& label : outlineLabels_) outline_.Items().Append(winrt::box_value(winrt::hstring(label)));
    }

    void EditorSidebarController::RefreshDiagnostics()
    {
        if (!session_ || !diagnostics_) return;
        std::vector<std::wstring> labels;
        std::vector<std::size_t> offsets;
        for (auto const& diagnostic : session_->Core().renderModel.diagnostics)
        {
            auto severity = L"Warning";
            if (diagnostic.severity == elmd::RenderDiagnostic::Sev::Info) severity = L"Info";
            else if (diagnostic.severity == elmd::RenderDiagnostic::Sev::Error) severity = L"Error";
            auto offset = diagnostic.source_range ? diagnostic.source_range->start.v : 0;
            auto label = winrt::hstring(severity) + L" @ " + winrt::to_hstring(offset) + L": " + winrt::to_hstring(diagnostic.message);
            labels.emplace_back(label.c_str());
            offsets.push_back(offset);
        }
        if (labels.empty())
        {
            labels.push_back(L"No diagnostics");
            offsets.push_back(0);
        }
        if (labels == diagnosticLabels_ && offsets == diagnosticOffsets_) return;
        diagnosticLabels_ = std::move(labels);
        diagnosticOffsets_ = std::move(offsets);
        diagnostics_.Items().Clear();
        for (auto const& label : diagnosticLabels_) diagnostics_.Items().Append(winrt::box_value(winrt::hstring(label)));
    }
}

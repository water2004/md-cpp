#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "settings/LatexCommandCatalog.h"

import folia.platform.editor_latex_completion_session;

namespace winrt::Folia
{
    class EditorLatexCompletionController
    {
    public:
        using InsertSnippet = std::function<bool(
            folia::NodeId,
            folia::SourceRange,
            std::u32string_view)>;

        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& editorSurface,
            winrt::Microsoft::UI::Xaml::Controls::Canvas const& overlay,
            winrt::Microsoft::UI::Xaml::Controls::Border const& popup,
            winrt::Microsoft::UI::Xaml::Controls::TextBlock const& prefixLabel,
            winrt::Microsoft::UI::Xaml::Controls::ListView const& list,
            std::shared_ptr<LatexCommandCatalog> catalog,
            InsertSnippet insertSnippet);
        void Detach();
        void SetEnabled(bool enabled);
        void Refresh();
        bool HandleKey(winrt::Windows::System::VirtualKey key);
        bool AcceptSelected();
        void Cancel();
        bool Active() const { return completion_.Active(); }

    private:
        void RefreshItems();
        void UpdatePosition();
        winrt::hstring CandidateDescription(folia::LatexCommandDefinition const& command) const;

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel editorSurface_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Canvas overlay_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border popup_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock prefixLabel_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ListView list_{ nullptr };
        std::shared_ptr<LatexCommandCatalog> catalog_;
        InsertSnippet insertSnippet_;
        folia::platform::editor::EditorLatexCompletionSession completion_;
        winrt::event_token itemClickToken_{};
        bool enabled_ = true;
    };
}

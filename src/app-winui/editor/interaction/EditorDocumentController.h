#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorTextInputController.h"

namespace winrt::Folia
{
    struct EditorDocumentController
    {
        using ExecuteCommand = std::function<bool(folia::Command const&)>;
        using SetStatus = std::function<void(winrt::hstring const&)>;
        using SetProgress = std::function<void(
            bool active,
            std::optional<double> value,
            bool cancellable)>;
        using DocumentChanged = std::function<void()>;
        using Render = std::function<void()>;
        using WindowHandle = std::function<HWND()>;

        EditorDocumentController();
        ~EditorDocumentController();
        EditorDocumentController(EditorDocumentController const&) = delete;
        EditorDocumentController& operator=(EditorDocumentController const&) = delete;

        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            EditorTextInputController& textInput,
            ExecuteCommand executeCommand,
            SetStatus setStatus,
            SetProgress setProgress,
            DocumentChanged documentChanged,
            Render render,
            WindowHandle windowHandle);
        void Detach();
        void OpenDocument();
        void SaveDocument();
        void SaveDocumentAs();
        void ExportPdf();
        void CancelOperation();
        void InsertImage();
        void OpenLink(std::string href);
        void CopySelection();
        void CutSelection();
        void PasteClipboard();

    private:
        struct State;
        static bool Active(std::shared_ptr<State> const& state, std::uint64_t generation);
        static winrt::fire_and_forget OpenDocumentAsync(std::shared_ptr<State> state, std::uint64_t generation);
        static winrt::fire_and_forget SaveDocumentAsync(std::shared_ptr<State> state, std::uint64_t generation);
        static winrt::fire_and_forget SaveDocumentAsAsync(std::shared_ptr<State> state, std::uint64_t generation);
        static winrt::fire_and_forget ExportPdfAsync(std::shared_ptr<State> state, std::uint64_t generation);
        static winrt::fire_and_forget InsertImageAsync(std::shared_ptr<State> state, std::uint64_t generation);
        static winrt::fire_and_forget OpenLinkAsync(std::shared_ptr<State> state, std::uint64_t generation, std::string href);
        static winrt::fire_and_forget PasteClipboardAsync(std::shared_ptr<State> state, std::uint64_t generation);

        std::shared_ptr<State> state_;
    };
}

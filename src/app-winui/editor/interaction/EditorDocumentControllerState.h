#pragma once

#include "editor/interaction/EditorDocumentController.h"

namespace winrt::Folia
{
    namespace detail
    {
        struct PdfExportWork
        {
            std::stop_source stop;
            std::atomic_bool done = false;
            std::atomic_bool cancelled = false;
            std::atomic_size_t completedPages = 0;
            std::atomic_size_t totalPages = 0;
            std::exception_ptr failure;
        };
    }

    struct EditorDocumentController::State
    {
        std::atomic_uint64_t generation = 1;
        EditorSession* session = nullptr;
        EditorSurfaceRenderer* renderer = nullptr;
        EditorTextInputController* textInput = nullptr;
        ExecuteCommand executeCommand;
        SetStatus setStatus;
        SetProgress setProgress;
        DocumentChanged documentChanged;
        Render render;
        WindowHandle windowHandle;
        std::atomic_bool cancelRequested = false;
        std::atomic_bool pdfExporting = false;
        std::filesystem::path pdfOutputPath;
        std::shared_ptr<detail::PdfExportWork> pdfWork;
    };
}

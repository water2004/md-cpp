#pragma once

#include <cstddef>

namespace winrt::Folia
{
    enum class EditorPdfExportResult
    {
        WaitingForAssets,
        InProgress,
        Completed,
    };

    struct EditorPdfExportProgress
    {
        EditorPdfExportResult result = EditorPdfExportResult::WaitingForAssets;
        std::size_t completedPages = 0;
        std::size_t totalPages = 0;
    };
}

#pragma once

import folia.core.render_model;
import folia.core.selection;
import folia.core.text_edit;
import folia.platform.editor_snippet_session;

namespace winrt::Folia::detail
{
    struct EditorSearchHighlight
    {
        folia::TextSpan span;
        bool current = false;
    };

    struct EditorRenderFrame
    {
        folia::RenderModel const& renderModel;
        folia::TextSelection selection;
        std::span<const EditorSearchHighlight> searchHighlights;
        std::wstring const& baseDirectory;
        std::function<void(std::size_t, std::size_t)> materializeBlocks;
        std::function<void(std::size_t, std::size_t)> releaseBlocksOutside;
        std::span<const folia::platform::editor::EditorSnippetPlaceholder> snippetPlaceholders;
    };
}

#pragma once

import elmd.core.render_model;
import elmd.core.selection;
import elmd.core.text_edit;

namespace winrt::ElMd::detail
{
    struct EditorSearchHighlight
    {
        elmd::TextSpan span;
        bool current = false;
    };

    struct EditorRenderFrame
    {
        elmd::RenderModel const& renderModel;
        elmd::TextSelection selection;
        std::span<const EditorSearchHighlight> searchHighlights;
        std::wstring const& baseDirectory;
        std::function<void(std::size_t, std::size_t)> materializeBlocks;
        std::function<void(std::size_t, std::size_t)> releaseBlocksOutside;
    };
}

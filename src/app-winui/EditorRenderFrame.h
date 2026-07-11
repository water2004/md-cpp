#pragma once

import elmd.core.render_model;
import elmd.core.selection;

namespace winrt::ElMd::detail
{
    struct EditorRenderFrame
    {
        elmd::RenderModel const& renderModel;
        std::u32string_view sourceText;
        elmd::Selection selection;
        std::wstring const& baseDirectory;
    };
}

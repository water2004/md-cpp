#pragma once

import elmd.core.render_model;
import elmd.core.selection;
import elmd.core.text_edit;

namespace winrt::ElMd::detail
{
    struct EditorRenderFrame
    {
        elmd::RenderModel const& renderModel;
        std::u32string_view sourceText;
        elmd::TextSelection selection;
        std::wstring const& baseDirectory;
    };
}

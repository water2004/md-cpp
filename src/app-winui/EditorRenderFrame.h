#pragma once

import elmd.core.render_model;
import elmd.core.selection;
import elmd.core.text_edit;

namespace winrt::ElMd::detail
{
    struct EditorRenderFrame
    {
        elmd::RenderModel const& renderModel;
        elmd::TextSelection selection;
        std::wstring const& baseDirectory;
    };
}

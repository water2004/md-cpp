#pragma once

import folia.core.latex_command_catalog;

namespace winrt::Folia
{
    struct LatexCommandFormSubmission
    {
        std::optional<std::string> editingId;
        std::u32string trigger;
        std::u32string snippet;
        std::string description;
    };

    Microsoft::UI::Xaml::FrameworkElement BuildLatexCommandSettingsRow(
        folia::LatexCommandDefinition const& command,
        double recentScore,
        std::function<void(std::string)> edit,
        std::function<void(std::string)> remove);
}

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

    class LatexCommandEditorForm
    {
    public:
        using Submit = std::function<bool(LatexCommandFormSubmission)>;

        Microsoft::UI::Xaml::FrameworkElement Build(Submit submit);
        void BeginEdit(folia::LatexCommandDefinition const& command);
        void Reset();
        bool Editing(std::string_view id) const;

    private:
        Microsoft::UI::Xaml::FrameworkElement root_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBox triggerBox_;
        Microsoft::UI::Xaml::Controls::TextBox templateBox_;
        Microsoft::UI::Xaml::Controls::TextBox descriptionBox_;
        Microsoft::UI::Xaml::Controls::Button saveButton_;
        Microsoft::UI::Xaml::Controls::Button cancelButton_;
        Submit submit_;
        std::optional<std::string> editingId_;
    };

    Microsoft::UI::Xaml::FrameworkElement BuildLatexCommandSettingsRow(
        folia::LatexCommandDefinition const& command,
        double recentScore,
        std::function<void(std::string)> edit,
        std::function<void(std::string)> remove);
}

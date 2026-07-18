#include "pch.h"
#include "editor/session/EditorSession.h"

import folia.core.editor;
import folia.core.document_text;
import folia.core.document_interaction;
import folia.core.document_content_context;
import folia.core.document_footnotes;
import folia.core.render_builder;
import folia.core.render_model;
import folia.core.search;
import folia.core.source_editor;
import folia.core.source_render;
import folia.core.utf;

namespace winrt::Folia
{
    EditorSession::EditorSession() : core_(std::make_unique<detail::EditorSessionCore>())
    {
        RebuildCore();
    }

    EditorSession::~EditorSession() = default;
    EditorSession::EditorSession(EditorSession&&) noexcept = default;
    EditorSession& EditorSession::operator=(EditorSession&&) noexcept = default;

    void EditorSession::Open(
        winrt::Windows::Storage::StorageFile const& file,
        winrt::hstring const& text,
        LoadProgress progress)
    {
        file_ = file;
        text_ = text;
        ++revision_;
        RebuildCore(std::move(progress));
    }

    void EditorSession::SaveAs(winrt::Windows::Storage::StorageFile const& file)
    {
        file_ = file;
        core_->baseDirectory = std::filesystem::path(file_.Path().c_str()).parent_path().wstring();
    }

    void EditorSession::SetText(winrt::hstring const& text)
    {
        text_ = text;
        ++revision_;
        RebuildCore();
    }

    void EditorSession::SetTheme(folia::ThemeProfile const& theme)
    {
        core_->theme = theme;
        RebuildRenderModel();
    }

    void EditorSession::RebuildCore(LoadProgress progress)
    {
        ClearSearch();
        core_->renderedSearchFragments.clear();
        core_->renderedSearchRevision = (std::numeric_limits<std::uint64_t>::max)();
        core_->baseDirectory = file_ ? std::filesystem::path(file_.Path().c_str()).parent_path().wstring() : std::wstring{};
        core_->sourceEditor.reset();
        core_->editor = folia::Editor(
            winrt::to_string(text_),
            folia::default_dialect(),
            std::move(progress));
        text_ = {};
        RefreshCharacterCount();
        RebuildRenderModel();
    }

    void EditorSession::RebuildRenderModel(folia::EditorDocumentChange const* change)
    {
        if (core_->sourceEditor)
        {
            core_->renderModel = folia::build_source_render_model_incremental(
                *core_->sourceEditor,
                std::move(core_->renderModel));
        }
        else if (change)
        {
            folia::RenderModelUpdate update;
            update.structural = change->structural;
            update.structural_locality_known = change->structural_locality_known;
            update.structural_anchors = change->structural_anchors;
            std::unordered_set<std::uint64_t> owners;
            for (auto const& operation : change->text_operations)
            {
                auto const& edit = change->forward ? operation.forward : operation.inverse;
                if (owners.insert(edit.container_id.v).second)
                {
                    update.changed_owners.push_back(edit.container_id);
                }
            }
            core_->renderModel = folia::build_render_model_incremental(
                core_->editor.document(),
                core_->editor.outline(),
                core_->editor.symbols(),
                core_->theme,
                std::move(core_->renderModel),
                update);
        }
        else
        {
            core_->renderModel = ShouldVirtualizeRenderModel()
                ? folia::build_virtualized_render_model(
                    core_->editor.document(),
                    core_->editor.outline(),
                    core_->editor.symbols(),
                    core_->theme)
                : folia::build_render_model(
                    core_->editor.document(),
                    core_->editor.outline(),
                    core_->editor.symbols(),
                    core_->theme);
        }
        core_->renderModel.revision = revision_;
    }

    bool EditorSession::ShouldVirtualizeRenderModel() const
    {
        if (!core_ || core_->sourceEditor) return false;
        auto const& document = core_->editor.document();
        return document.root.children.size() > 4096
            || document.cached_editable_order.size() > 8192;
    }

    void EditorSession::MaterializeRenderBlocks(std::size_t begin, std::size_t end)
    {
        if (!core_ || core_->sourceEditor || !core_->renderModel.virtualized) return;
        folia::materialize_render_model_range(
            core_->renderModel,
            core_->editor.document(),
            core_->editor.symbols(),
            core_->theme,
            begin,
            end);
    }

    void EditorSession::ReleaseRenderBlocksOutside(std::size_t begin, std::size_t end)
    {
        if (!core_ || core_->sourceEditor || !core_->renderModel.virtualized) return;
        folia::release_render_model_blocks_outside(
            core_->renderModel,
            core_->editor.document(),
            core_->theme,
            begin,
            end);
    }

    void EditorSession::SetSelection(folia::TextPosition anchor, folia::TextPosition active)
    {
        if (core_->sourceEditor)
        {
            auto anchorOffset = core_->sourceEditor->source_offset_from_position(anchor);
            auto activeOffset = core_->sourceEditor->source_offset_from_position(active);
            if (!anchorOffset || !activeOffset) return;
            core_->sourceEditor->set_selection({*anchorOffset, *activeOffset, anchor.affinity, active.affinity});
            return;
        }
        auto anchorSource = core_->editor.editable_source(anchor.container_id);
        auto activeSource = core_->editor.editable_source(active.container_id);
        if (!anchorSource || !activeSource) return;
        anchor.source_offset = (std::min)(anchor.source_offset, anchorSource->size());
        active.source_offset = (std::min)(active.source_offset, activeSource->size());
        core_->editor.set_selection({anchor, active});
    }

    void EditorSession::SetSelection(folia::TextSelection selection)
    {
        SetSelection(selection.anchor, selection.active);
    }

    bool EditorSession::HasSelection() const
    {
        return core_->sourceEditor
            ? !core_->sourceEditor->selection().is_caret()
            : !core_->editor.selection().is_caret();
    }

    std::u32string EditorSession::SelectedSource() const
    {
        return core_->sourceEditor
            ? core_->sourceEditor->selected_text()
            : core_->editor.selected_markdown_cps();
    }

    std::string EditorSession::SelectedTextUtf8() const
    {
        return folia::cps_to_utf8(SelectedSource());
    }

    bool EditorSession::HasFile() const
    {
        return file_ != nullptr;
    }

    winrt::Windows::Storage::StorageFile EditorSession::File() const
    {
        return file_;
    }

    winrt::hstring EditorSession::Text() const
    {
        if (!core_) return text_;
        return winrt::to_hstring(core_->sourceEditor
            ? folia::cps_to_utf8(core_->sourceEditor->source())
            : core_->editor.markdown_utf8());
    }

    winrt::hstring EditorSession::DisplayName() const
    {
        return file_ ? file_.Name() : L"Untitled.md";
    }

    winrt::hstring EditorSession::Path() const
    {
        return file_ ? file_.Path() : L"";
    }

    uint64_t EditorSession::Revision() const
    {
        return revision_;
    }

    void EditorSession::RefreshCharacterCount()
    {
        core_->characterCount = core_->sourceEditor
            ? core_->sourceEditor->source().size()
            : folia::document_text_character_count(core_->editor.document());
    }

    std::size_t EditorSession::CharacterCount() const
    {
        return core_->characterCount;
    }

    std::optional<EditorDocumentInteraction> EditorSession::InteractionAt(folia::TextPosition position) const
    {
        if (core_->sourceEditor) return std::nullopt;
        auto interaction = folia::document_interaction_at(core_->editor.document(), position);
        if (!interaction) return std::nullopt;
        return EditorDocumentInteraction{
            std::move(interaction->link),
            std::move(interaction->tooltip),
        };
    }

    folia::TextSelection EditorSession::Selection() const
    {
        return core_->sourceEditor ? core_->sourceEditor->projected_selection() : core_->editor.selection();
    }

    folia::platform::editor::EditorShortcutScope EditorSession::ShortcutScope() const
    {
        using folia::DocumentContentContext;
        using folia::platform::editor::EditorShortcutScope;
        if (core_->sourceEditor) return EditorShortcutScope::Global;
        switch (folia::document_content_context_at(
            core_->editor.document(), core_->editor.selection().active))
        {
            case DocumentContentContext::Code: return EditorShortcutScope::Code;
            case DocumentContentContext::Math: return EditorShortcutScope::Math;
            case DocumentContentContext::Normal: return EditorShortcutScope::Global;
        }
        return EditorShortcutScope::Global;
    }

    std::optional<EditorLatexCompletionSource> EditorSession::LatexCompletionSourceAtCaret() const
    {
        if (core_->sourceEditor) return std::nullopt;
        auto selection = core_->editor.selection();
        if (selection.anchor != selection.active) return std::nullopt;
        if (folia::document_content_context_at(core_->editor.document(), selection.active)
            != folia::DocumentContentContext::Math)
            return std::nullopt;
        auto source = folia::document_editable_text_view(
            core_->editor.document(), selection.active.container_id);
        if (!source) return std::nullopt;
        return EditorLatexCompletionSource{
            .container = selection.active.container_id,
            .source = *source,
            .caret = selection.active.source_offset,
        };
    }

    folia::RenderModel const& EditorSession::RenderModel() const
    {
        return core_->renderModel;
    }

    folia::RenderModel EditorSession::BuildPrintRenderModel() const
    {
        if (core_->sourceEditor) return core_->renderModel;
        return folia::build_render_model(
            core_->editor.document(),
            core_->editor.outline(),
            core_->editor.symbols(),
            core_->theme);
    }

    std::optional<folia::TextPosition> EditorSession::FootnoteDefinitionTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return folia::footnote_definition_target(
            core_->editor.document(), core_->editor.symbols(), label);
    }

    std::optional<folia::TextPosition> EditorSession::FirstFootnoteReferenceTarget(std::string_view label) const
    {
        if (core_->sourceEditor) return std::nullopt;
        return folia::first_footnote_reference_target(
            core_->editor.symbols(), label);
    }

    std::string EditorSession::FootnotePreview(std::string_view label) const
    {
        if (core_->sourceEditor) return {};
        return folia::footnote_preview(
            core_->editor.document(), core_->editor.symbols(), label, 240);
    }

    std::wstring const& EditorSession::BaseDirectory() const
    {
        return core_->baseDirectory;
    }

    detail::EditorRenderFrame EditorSession::RenderFrame()
    {
        return detail::EditorRenderFrame{
            core_->renderModel,
            Selection(),
            core_->searchHighlights,
            core_->baseDirectory,
            core_->renderModel.virtualized
                ? std::function<void(std::size_t, std::size_t)>{
                    [this](auto begin, auto end) { MaterializeRenderBlocks(begin, end); }}
                : std::function<void(std::size_t, std::size_t)>{},
            core_->renderModel.virtualized
                ? std::function<void(std::size_t, std::size_t)>{
                    [this](auto begin, auto end) { ReleaseRenderBlocksOutside(begin, end); }}
                : std::function<void(std::size_t, std::size_t)>{},
        };
    }
}

// elmd.core.document_interaction — block-local interactive metadata queries.
//
// Pointer interaction already supplies the authoritative block-local source
// position. Resolve links and tooltips from that owner's CST instead of
// walking the complete render projection.
export module elmd.core.document_interaction;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.text_edit;

export namespace elmd {

struct DocumentInteraction {
    std::optional<std::string> link;
    std::optional<std::string> tooltip;
};

inline std::optional<DocumentInteraction> document_interaction_at(
    const EditorDocument& document,
    TextPosition position) {
    const auto* block = find_document_block(document, position.container_id);
    if (!block) return std::nullopt;

    DocumentInteraction interaction;
    if (block->kind == BlockKind::ImageBlock) {
        auto const& special = block->image_special();
        interaction.link = special.image_link;
        if (special.image_title && !special.image_title->empty()) {
            interaction.tooltip = special.image_title;
        } else if (!special.image_alt.empty()) {
            interaction.tooltip = special.image_alt;
        }
        return interaction.link || interaction.tooltip
            ? std::optional{std::move(interaction)}
            : std::nullopt;
    }

    const auto* inline_document = editable_inline_document(*block);
    if (!inline_document || position.source_offset > inline_document->source.size()) {
        return std::nullopt;
    }

    auto visit = [&](auto& self, const InlineCstNodes& nodes) -> bool {
        for (const auto& node : nodes) {
            if (!node.range.covers(position.source_offset)) continue;
            switch (node.kind) {
                case InlineCstKind::Link:
                case InlineCstKind::Autolink:
                    if (!node.semantics().href.empty()) interaction.link = node.semantics().href;
                    if (node.semantics().title && !node.semantics().title->empty()) interaction.tooltip = node.semantics().title;
                    else if (!node.semantics().href.empty()) interaction.tooltip = node.semantics().href;
                    break;
                case InlineCstKind::Image:
                    if (node.semantics().title && !node.semantics().title->empty()) interaction.tooltip = node.semantics().title;
                    else if (!node.semantics().alt.empty()) interaction.tooltip = node.semantics().alt;
                    break;
                default:
                    break;
            }
            self(self, node.children);
            return true;
        }
        return false;
    };
    visit(visit, inline_document->tree.nodes);
    return interaction.link || interaction.tooltip
        ? std::optional{std::move(interaction)}
        : std::nullopt;
}

} // namespace elmd

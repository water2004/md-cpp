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
        interaction.link = block->image_link;
        if (block->image_title && !block->image_title->empty()) {
            interaction.tooltip = block->image_title;
        } else if (!block->image_alt.empty()) {
            interaction.tooltip = block->image_alt;
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
                    if (!node.href.empty()) interaction.link = node.href;
                    if (node.title && !node.title->empty()) interaction.tooltip = node.title;
                    else if (!node.href.empty()) interaction.tooltip = node.href;
                    break;
                case InlineCstKind::Image:
                    if (node.title && !node.title->empty()) interaction.tooltip = node.title;
                    else if (!node.alt.empty()) interaction.tooltip = node.alt;
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

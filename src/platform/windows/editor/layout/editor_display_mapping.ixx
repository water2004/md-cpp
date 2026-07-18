// elmd.platform.editor_display_mapping — source/display coordinate bridge.
export module elmd.platform.editor_display_mapping;
import std;
export import elmd.core.ids;
export import elmd.core.selection;
export import elmd.core.text_edit;

export namespace elmd::platform::editor
{
    enum class EditorDisplayPositionKind
    {
        Source,
        Generated,
        BoundaryDecoration,
    };

    enum class EditorFootnoteControlKind
    {
        Reference,
        DefinitionLabel,
    };

    struct EditorDisplayPosition : elmd::TextPosition
    {
        EditorDisplayPositionKind kind = EditorDisplayPositionKind::Source;

        EditorDisplayPosition() = default;
        EditorDisplayPosition(
            elmd::TextPosition position,
            EditorDisplayPositionKind displayKind = EditorDisplayPositionKind::Source)
            : elmd::TextPosition(position), kind(displayKind)
        {
        }
        EditorDisplayPosition(
            elmd::NodeId containerId,
            std::size_t sourceOffset,
            elmd::TextAffinity affinity,
            EditorDisplayPositionKind displayKind = EditorDisplayPositionKind::Source)
            : elmd::TextPosition{containerId, sourceOffset, affinity}, kind(displayKind)
        {
        }
    };

    using EditorDisplayMapping = std::vector<EditorDisplayPosition>;

    inline std::size_t DisplayPositionForSource(
        EditorDisplayMapping const& mapping,
        elmd::TextPosition sourcePosition)
    {
        if (mapping.empty()) return 0;

        std::optional<std::size_t> lastInContainer;
        std::optional<std::size_t> exactSource;
        std::optional<std::size_t> exactGeneratedAffinity;
        for (std::size_t index = 0; index < mapping.size(); ++index)
        {
            auto const& point = mapping[index];
            if (point.container_id != sourcePosition.container_id) continue;
            lastInContainer = index;
            if (point.source_offset < sourcePosition.source_offset) continue;
            if (point.source_offset > sourcePosition.source_offset)
                return exactGeneratedAffinity.value_or(exactSource.value_or(index));

            if (point.kind == EditorDisplayPositionKind::Source)
            {
                if (point.affinity == sourcePosition.affinity) return index;
                if (!exactSource) exactSource = index;
            }
            else if (point.affinity == sourcePosition.affinity)
            {
                // A downstream boundary decoration is a generated prefix
                // (quote/list/callout indentation, bullet, task control, ...).
                // The source position begins after the complete prefix, not
                // before its first glyph. Empty content nodes have no Source
                // mapping to supersede this candidate, which is why forward
                // cross-block movement previously drew their caret at the
                // far-left container edge. Upstream decorations are suffixes;
                // they intentionally keep the first boundary before the run.
                if (point.kind == EditorDisplayPositionKind::BoundaryDecoration
                    && sourcePosition.affinity == elmd::TextAffinity::Downstream)
                {
                    exactGeneratedAffinity = (std::min)(index + 1, mapping.size() - 1);
                }
                else if (!exactGeneratedAffinity)
                {
                    exactGeneratedAffinity = index;
                }
            }
        }
        return exactGeneratedAffinity.value_or(exactSource.value_or(lastInContainer.value_or(0)));
    }
}

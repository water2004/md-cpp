// folia.core.source_style — non-authoritative styling spans for source mode.
// Source mode always renders the exact source characters. These spans may
// change presentation only; they never replace text or participate in edits.
export module folia.core.source_style;
import std;
import folia.core.text_edit;

export namespace folia {

enum class SourceSyntaxKind {
    None,
    Marker,
    Heading,
    Emphasis,
    Strong,
    Strikethrough,
    Link,
    Code,
    Math,
    Escape,
    Entity,
    Error,
};

struct SourceStyleSpan {
    SourceRange range;
    SourceSyntaxKind kind = SourceSyntaxKind::None;

    bool operator==(SourceStyleSpan const&) const = default;
};

} // namespace folia

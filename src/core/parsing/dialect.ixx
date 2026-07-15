// elmd.core.dialect — Markdown dialect configuration + related enums.
export module elmd.core.dialect;
import std;

export namespace elmd {

enum class MathRenderBackend { Native, SvgRasterized, PlainTextFallback };
enum class RawHtmlPolicy { DisabledTreatAsText, DisabledWarnAsUnsupported };
enum class MathDelimiter { InlineDollar, BlockDollar, InlineParen, BlockBracket, FencedMath };
enum class TocMarkerKind { BracketToc, WikiToc };
enum class FrontmatterFormat { Yaml, Toml, Json };
enum class TableAlignment { Left, Center, Right, None };
enum class UnsupportedMarkupReason { RawHtmlDisabled, UnknownExtension, MalformedSyntax };

inline const char* unsupported_reason_message(UnsupportedMarkupReason r) {
    switch (r) {
        case UnsupportedMarkupReason::RawHtmlDisabled:  return "raw_html_disabled";
        case UnsupportedMarkupReason::UnknownExtension: return "unknown_extension";
        case UnsupportedMarkupReason::MalformedSyntax:  return "malformed_syntax";
    }
    return "unknown";
}

struct GfmOptions       { bool tables=true, task_lists=true, strikethrough=true, autolinks=true; };
struct MathOptions      { bool inline_dollar=true, block_dollar=true, inline_paren=true, block_bracket=true, fenced_math=true;
                          MathRenderBackend render_backend = MathRenderBackend::PlainTextFallback; };
struct TocOptions       { bool bracket_toc=true, wiki_toc=true, generate_slugs=true; };
struct FrontmatterOptions { bool yaml=true, toml=true, json=true; };
struct TableOptions     { bool gfm_pipe_tables=true, editable_grid_model=true; };
struct ImageOptions     { bool local_images=true, remote_images=false, drag_drop_assets=true; };
struct DiagramOptions   { bool mermaid=false, graphviz=false, fallback_to_code_block=true; };

struct MarkdownDialect {
    bool commonmark = true;
    GfmOptions gfm{};
    MathOptions math{};
    TocOptions toc{};
    FrontmatterOptions frontmatter{};
    bool footnotes = true;
    bool definition_lists = true;
    bool callouts = true;
    bool wiki_links = true;
    TableOptions tables{};
    ImageOptions images{};
    DiagramOptions diagrams{};
    RawHtmlPolicy raw_html = RawHtmlPolicy::DisabledTreatAsText;
};

inline MarkdownDialect default_dialect() { return MarkdownDialect{}; }

} // namespace elmd
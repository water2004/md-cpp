// elmd.core.exporter — Markdown / HTML / PlainText export pipeline.
// Pure core, portable. Raw HTML is always escaped on export (project gate).
// No WinUI / Windows / DirectWrite dependency.
// (module name avoids the `export` keyword: `elmd.core.exporter`.)
export module elmd.core.exporter;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.settings;
import elmd.core.error;
import elmd.core.utf;

export namespace elmd {

enum class ExportFormat { Markdown, Html, Pdf, Docx, PlainText };

struct ExportOptions {
    bool expand_toc = false;
    bool render_math = false;
    bool include_frontmatter = true;
    AssetExportPolicy asset_policy = AssetExportPolicy::CopyRelative;
    ExportRawHtmlPolicy raw_html_policy = ExportRawHtmlPolicy::EscapeAsText;
};

struct ExportArtifact {
    std::string content;
    std::vector<std::byte> content_bytes;
    std::string mime_type;
    std::string extension;
};

// Escape raw HTML so user-authored <script>/<div>/… is never injected verbatim.
inline std::string escape_raw_html(std::string_view text) {
    std::string out; out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

inline std::string escape_text(std::string_view text) {
    std::string out; out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

inline EditorResult<ExportArtifact> export_markdown(std::string_view source,
                                                    const MarkdownDocument&,
                                                    const ExportOptions&) {
    ExportArtifact a;
    a.content = std::string(source);
    a.mime_type = "text/markdown";
    a.extension = "md";
    return a;
}

inline EditorResult<ExportArtifact> export_plain_text(std::string_view source) {
    ExportArtifact a;
    a.content = std::string(source);
    a.mime_type = "text/plain";
    a.extension = "txt";
    return a;
}

namespace detail {

inline std::string sanitized_target(std::string_view value, bool image) {
    std::size_t start = 0;
    std::size_t end = value.size();
    while (start < end && static_cast<unsigned char>(value[start]) <= 0x20) ++start;
    while (end > start && static_cast<unsigned char>(value[end - 1]) <= 0x20) --end;
    std::string result(value.substr(start, end - start));
    std::string lower = result;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    auto colon = lower.find(':');
    auto boundary = lower.find_first_of("/?#");
    if (colon != std::string::npos && (boundary == std::string::npos || colon < boundary)) {
        auto scheme = lower.substr(0, colon);
        auto allowed = scheme == "http" || scheme == "https" || (!image && scheme == "mailto") || (image && scheme == "data" && lower.starts_with("data:image/"));
        if (!allowed) return {};
    }
    return result;
}

inline std::string image_dimension_attributes(std::optional<float> width, std::optional<float> height) {
    std::string attributes;
    if (width) attributes += " width=\"" + std::to_string(*width) + "\"";
    if (height) attributes += " height=\"" + std::to_string(*height) + "\"";
    return attributes;
}

inline std::string inline_to_html(const InlineNode& n, ExportRawHtmlPolicy pol) {
    using K = InlineKind;
    switch (n.kind) {
        case K::Text: return escape_text(cps_to_utf8(n.text));
        case K::Strong: {
            std::string s;
            for (const auto& c : n.children) s += inline_to_html(c, pol);
            return "<strong>" + s + "</strong>";
        }
        case K::Emphasis: {
            std::string s;
            for (const auto& c : n.children) s += inline_to_html(c, pol);
            return "<em>" + s + "</em>";
        }
        case K::Strike: {
            std::string s;
            for (const auto& c : n.children) s += inline_to_html(c, pol);
            return "<del>" + s + "</del>";
        }
        case K::Span: {
            std::string s;
            for (const auto& c : n.children) s += inline_to_html(c, pol);
            return s;
        }
        case K::InlineCode:
            return "<code>" + escape_raw_html(cps_to_utf8(n.text)) + "</code>";
        case K::Link: {
            std::string s;
            for (const auto& c : n.children) s += inline_to_html(c, pol);
            std::string title_attr;
            if (n.title) title_attr = " title=\"" + escape_text(*n.title) + "\"";
            std::string href = escape_text(sanitized_target(n.href, false));
            return "<a href=\"" + href + "\"" + title_attr + ">" + s + "</a>";
        }
        case K::Image: {
            std::string title_attr;
            if (n.title) title_attr = " title=\"" + escape_text(*n.title) + "\"";
            return "<img src=\"" + escape_text(sanitized_target(n.href, true)) + "\" alt=\"" + escape_text(n.alt) + "\"" + title_attr + image_dimension_attributes(n.image_width, n.image_height) + " />";
        }
        case K::InlineMath:
            return "<span class=\"math-inline\">" + escape_raw_html(cps_to_utf8(n.text)) + "</span>";
        case K::FootnoteRef:
            return "<sup>" + escape_text("[^" + n.label + "]") + "</sup>";
        case K::WikiLink:
            return "<a href=\"" + escape_text(n.target) + "\">" + escape_text(n.alias ? *n.alias : n.target) + "</a>";
        case K::SoftBreak: return "<br>\n";
        case K::HardBreak: return "<br>\n";
        case K::UnsupportedMarkup:
            return pol == ExportRawHtmlPolicy::Drop ? std::string{} : escape_raw_html(cps_to_utf8(n.text));
        case K::Extension: return escape_text("[ext:" + n.ext_name + "]");
    }
    return {};
}

inline std::string block_to_html(const BlockNode& b, ExportRawHtmlPolicy pol);

inline std::string blocks_to_html(const BlockVec& v, ExportRawHtmlPolicy pol) {
    std::string s;
    for (const auto& c : v) s += block_to_html(c, pol);
    return s;
}

inline std::string block_to_html(const BlockNode& b, ExportRawHtmlPolicy pol) {
    using BK = BlockKind;
    switch (b.kind) {
        case BK::Heading: {
            std::string s;
            for (const auto& c : b.children) s += inline_to_html(c, pol);
            std::string h = "h" + std::to_string(b.level);
            return "<" + h + " id=\"" + escape_text(b.slug) + "\">" + s + "</" + h + ">\n";
        }
        case BK::Paragraph: {
            std::string s;
            for (const auto& c : b.children) s += inline_to_html(c, pol);
            return "<p>" + s + "</p>\n";
        }
        case BK::CodeBlock: {
            std::string lang_attr;
            if (b.language) lang_attr = " class=\"language-" + escape_text(*b.language) + "\"";
            return "<pre><code" + lang_attr + ">" + escape_raw_html(cps_to_utf8(b.code_text)) + "</code></pre>\n";
        }
        case BK::BlockQuote:
            return "<blockquote>\n" + blocks_to_html(b.quote_children, pol) + "</blockquote>\n";
        case BK::List: {
            std::string items;
            for (const auto& it : b.list_items) items += "<li>" + blocks_to_html(it.children, pol) + "</li>\n";
            if (b.list_ordered) {
                auto start = b.list_start == 1 ? std::string{} : " start=\"" + std::to_string(b.list_start) + "\"";
                return "<ol" + start + ">\n" + items + "</ol>\n";
            }
            return "<ul>\n" + items + "</ul>\n";
        }
        case BK::TaskList: {
            std::string items;
            for (const auto& it : b.task_items) {
                items += "<li>" + std::string(it.checked ? "[x] " : "[ ] ") + blocks_to_html(it.children, pol) + "</li>\n";
            }
            return "<ul class=\"task-list\">\n" + items + "</ul>\n";
        }
        case BK::MathBlock:
            return "<div class=\"math-block\">" + escape_raw_html(cps_to_utf8(b.tex)) + "</div>\n";
        case BK::Table: {
            std::string html = "<table>\n<thead><tr>";
            for (const auto& c : b.table_header) {
                std::string s;
                for (const auto& in : c.children) s += inline_to_html(in, pol);
                html += "<th>" + s + "</th>";
            }
            html += "</tr></thead>\n<tbody>\n";
            for (const auto& row : b.table_rows) {
                html += "<tr>";
                for (const auto& c : row.cells) {
                    std::string s;
                    for (const auto& in : c.children) s += inline_to_html(in, pol);
                    html += "<td>" + s + "</td>";
                }
                html += "</tr>\n";
            }
            html += "</tbody>\n</table>\n";
            return html;
        }
        case BK::ThematicBreak: return "<hr>\n";
        case BK::LinkDefinition: return {};
        case BK::Frontmatter:
            return pol == ExportRawHtmlPolicy::Drop ? std::string{} : escape_raw_html(b.raw);
        case BK::UnsupportedMarkup:
            return pol == ExportRawHtmlPolicy::Drop ? std::string{} : escape_raw_html(b.raw);
        case BK::Toc: return "<nav class=\"toc\"></nav>\n";
        case BK::Callout: {
            std::string s = "<div class=\"callout callout-" + escape_text(b.callout_kind) + "\">\n";
            s += blocks_to_html(b.quote_children, pol);
            s += "</div>\n";
            return s;
        }
        case BK::FootnoteDefinition: {
            std::string s = "<section class=\"footnote\" id=\"fn-" + escape_text(b.footnote_label) + "\">\n";
            s += blocks_to_html(b.quote_children, pol);
            s += "</section>\n";
            return s;
        }
        case BK::ImageBlock: {
            auto title = b.image_title ? " title=\"" + escape_text(*b.image_title) + "\"" : std::string{};
            auto image = "<img src=\"" + escape_text(sanitized_target(b.src, true)) + "\" alt=\"" + escape_text(b.image_alt) + "\"" + title + image_dimension_attributes(b.image_width, b.image_height) + " />";
            if (b.image_link) image = "<a href=\"" + escape_text(sanitized_target(*b.image_link, false)) + "\">" + image + "</a>";
            return image + "\n";
        }
        case BK::Extension:
            return "<div class=\"extension\">" + escape_text(b.ext_name) + "</div>\n";
    }
    return {};
}

} // namespace detail

inline EditorResult<ExportArtifact> export_html(std::string_view /*source*/,
                                                const MarkdownDocument& doc,
                                                const ExportOptions& opts) {
    std::string html = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
    if (doc.metadata.title) html += "<title>" + escape_text(*doc.metadata.title) + "</title>\n";
    html += "</head>\n<body>\n";
    for (const auto& b : doc.blocks) html += detail::block_to_html(b, opts.raw_html_policy);
    html += "</body>\n</html>\n";
    ExportArtifact a;
    a.content = std::move(html);
    a.mime_type = "text/html";
    a.extension = "html";
    return a;
}

} // namespace elmd

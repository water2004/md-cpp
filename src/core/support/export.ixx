// elmd.core.exporter — Markdown / HTML / PlainText export pipeline.
// Pure core, portable. Raw HTML is always escaped on export (project gate).
// No WinUI / Windows / DirectWrite dependency.
// (module name avoids the `export` keyword: `elmd.core.exporter`.)
export module elmd.core.exporter;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.document;
import elmd.core.settings;
import elmd.core.serializer;
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

inline EditorResult<ExportArtifact> export_markdown(const EditorDocument& document,
                                                    const ExportOptions&) {
    ExportArtifact a;
    a.content = serialize_markdown(document);
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

inline std::string inline_nodes_to_html(
    const InlineDocument& document,
    const InlineCstNodes& nodes,
    ExportRawHtmlPolicy policy) {
    std::string html;
    for (const auto& node : nodes) {
        using K = InlineCstKind;
        const auto raw = cps_to_utf8(inline_source_slice(document, node.range));
        switch (node.kind) {
            case K::Strong:
                html += "<strong>" + inline_nodes_to_html(document, node.children, policy) + "</strong>";
                break;
            case K::Emphasis:
                html += "<em>" + inline_nodes_to_html(document, node.children, policy) + "</em>";
                break;
            case K::Strikethrough:
                html += "<del>" + inline_nodes_to_html(document, node.children, policy) + "</del>";
                break;
            case K::CodeSpan:
                html += "<code>" + escape_raw_html(cps_to_utf8(inline_source_slice(document, node.delimiter_ranges().content))) + "</code>";
                break;
            case K::InlineMath:
                html += "<span class=\"math-inline\">" + escape_raw_html(cps_to_utf8(inline_source_slice(document, node.delimiter_ranges().content))) + "</span>";
                break;
            case K::Link: {
                const auto& semantic = node.semantics();
                const auto title = semantic.title ? " title=\"" + escape_text(*semantic.title) + "\"" : std::string{};
                html += "<a href=\"" + escape_text(sanitized_target(semantic.href, false)) + "\"" + title + ">"
                    + inline_nodes_to_html(document, node.children, policy) + "</a>";
                break;
            }
            case K::Image: {
                const auto& semantic = node.semantics();
                const auto title = semantic.title ? " title=\"" + escape_text(*semantic.title) + "\"" : std::string{};
                html += "<img src=\"" + escape_text(sanitized_target(semantic.href, true)) + "\" alt=\""
                    + escape_text(semantic.alt) + "\"" + title
                    + image_dimension_attributes(semantic.image_width, semantic.image_height) + " />";
                break;
            }
            case K::HtmlElement:
                html += inline_nodes_to_html(document, node.children, policy);
                break;
            case K::Autolink:
                html += "<a href=\"" + escape_text(sanitized_target(node.semantics().href, false)) + "\">"
                    + escape_text(cps_to_utf8(inline_source_slice(document, node.delimiter_ranges().content))) + "</a>";
                break;
            case K::WikiLink:
                html += "<a href=\"" + escape_text(node.semantics().target) + "\">"
                    + escape_text(node.semantics().alias.value_or(node.semantics().target)) + "</a>";
                break;
            case K::FootnoteRef:
                html += "<sup>" + escape_text("[^" + node.semantics().label + "]") + "</sup>";
                break;
            case K::SoftBreak:
            case K::HardBreak:
                html += "<br>\n";
                break;
            case K::Escape: {
                const auto source = inline_source_slice(document, node.range);
                html += escape_text(cps_to_utf8(source.empty() ? source : source.substr(1)));
                break;
            }
            case K::Entity:
                html += escape_text(cps_to_utf8(decode_inline_entity(inline_source_slice(document, node.range))));
                break;
            case K::Raw:
                if (policy != ExportRawHtmlPolicy::Drop) html += escape_raw_html(raw);
                break;
            default:
                html += escape_text(raw);
                break;
        }
    }
    return html;
}

inline std::string inline_document_to_html(const InlineDocument& document, ExportRawHtmlPolicy policy) {
    return inline_nodes_to_html(document, document.tree.nodes, policy);
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
            std::string s = inline_document_to_html(b.inline_content, pol);
            std::string h = "h" + std::to_string(b.level);
            return "<" + h + " id=\"" + escape_text(b.slug) + "\">" + s + "</" + h + ">\n";
        }
        case BK::Paragraph: {
            std::string s = inline_document_to_html(b.inline_content, pol);
            return "<p>" + s + "</p>\n";
        }
        case BK::CodeBlock: {
            std::string lang_attr;
            if (b.block_source.tree().language) lang_attr = " class=\"language-" + escape_text(*b.block_source.tree().language) + "\"";
            return "<pre><code" + lang_attr + ">" + escape_raw_html(cps_to_utf8(block_source_content(b.block_source))) + "</code></pre>\n";
        }
        case BK::BlockQuote:
            return "<blockquote>\n" + blocks_to_html(b.children, pol) + "</blockquote>\n";
        case BK::List: {
            std::string items;
            for (const auto& it : b.children) items += "<li>" + blocks_to_html(it.children, pol) + "</li>\n";
            if (b.list_ordered) {
                auto start = b.list_start == 1 ? std::string{} : " start=\"" + std::to_string(b.list_start) + "\"";
                return "<ol" + start + ">\n" + items + "</ol>\n";
            }
            return "<ul>\n" + items + "</ul>\n";
        }
        case BK::TaskList: {
            std::string items;
            for (const auto& it : b.children) {
                items += "<li>" + std::string(it.checked ? "[x] " : "[ ] ") + blocks_to_html(it.children, pol) + "</li>\n";
            }
            return "<ul class=\"task-list\">\n" + items + "</ul>\n";
        }
        case BK::MathBlock:
            return "<div class=\"math-block\">" + escape_raw_html(cps_to_utf8(block_source_content(b.block_source))) + "</div>\n";
        case BK::Table: {
            if (b.children.empty()) return "<table></table>\n";
            std::string html = "<table>\n<thead><tr>";
            for (const auto& c : b.children.front().children) {
                std::string s = inline_document_to_html(c.inline_content, pol);
                html += "<th>" + s + "</th>";
            }
            html += "</tr></thead>\n<tbody>\n";
            for (std::size_t index = 1; index < b.children.size(); ++index) {
                const auto& row = b.children[index];
                html += "<tr>";
                for (const auto& c : row.children) {
                    std::string s = inline_document_to_html(c.inline_content, pol);
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
            s += blocks_to_html(b.children, pol);
            s += "</div>\n";
            return s;
        }
        case BK::FootnoteDefinition: {
            std::string s = "<section class=\"footnote\" id=\"fn-" + escape_text(b.footnote_label) + "\">\n";
            s += blocks_to_html(b.children, pol);
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
        case BK::Document:
        case BK::ListItem:
        case BK::TaskListItem:
            return blocks_to_html(b.children, pol);
        case BK::TableRow:
        case BK::TableCell:
            return {};
    }
    return {};
}

} // namespace detail

inline EditorResult<ExportArtifact> export_html(std::string_view /*source*/,
                                                const EditorDocument& doc,
                                                const ExportOptions& opts) {
    std::string html = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
    if (doc.metadata.title) html += "<title>" + escape_text(*doc.metadata.title) + "</title>\n";
    html += "</head>\n<body>\n";
    for (const auto& b : doc.root.children) html += detail::block_to_html(b, opts.raw_html_policy);
    html += "</body>\n</html>\n";
    ExportArtifact a;
    a.content = std::move(html);
    a.mime_type = "text/html";
    a.extension = "html";
    return a;
}

} // namespace elmd

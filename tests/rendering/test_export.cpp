#include <string>

#include "support/folia_test.hpp"
import elmd.core.exporter;
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.settings;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;


suite export_tests = [] {

"test_escape_raw_html_scripts"_test = [] {
    std::string input = "<script>alert(1)</script>";
    std::string esc = escape_raw_html(input);
    expect(fatal(bool(esc.find("<script>") == std::string::npos)));
    expect(fatal(bool(esc.find("&lt;script&gt;") != std::string::npos)));
};

"test_export_html_escapes_script"_test = [] {
    auto out = parse_text(1, "<script>alert(1)</script>\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    expect(fatal(bool(res.ok)));
    auto& html = res.value.content;
    expect(fatal(bool(html.find("<script>alert") == std::string::npos)));
    expect(fatal(bool(html.find("&lt;script&gt;") != std::string::npos)));
};

"test_export_html_escapes_div"_test = [] {
    auto out = parse_text(1, "<div>hello</div>\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    expect(fatal(bool(res.ok)));
    auto& html = res.value.content;
    expect(fatal(bool(html.find("<div>hello</div>\n") == std::string::npos)));
};

"test_export_html_uses_safe_semantics_for_recursive_html"_test = [] {
    auto out = parse_text(1,
        "<div onclick='evil()'><h2>Title</h2><p>safe <strong>text</strong> <mark>marked</mark></p></div>\n\n"
        "<table><tr><td>A</td><td>B</td></tr></table>\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    expect(fatal(bool(res.ok)));
    const auto& html = res.value.content;
    expect(fatal(bool(html.find("onclick") == std::string::npos)));
    expect(fatal(bool(html.find("<h2") != std::string::npos)));
    expect(fatal(bool(html.find("<strong>text</strong>") != std::string::npos)));
    expect(fatal(bool(html.find("<mark>marked</mark>") != std::string::npos)));
    expect(fatal(bool(html.find("<td>A</td><td>B</td>") != std::string::npos)));
    expect(fatal(bool(html.find("<th>A</th>") == std::string::npos)));
};

"test_export_markdown_serializes_document"_test = [] {
    std::string src = "# Title\n\nHello *world*.\n";
    auto out = parse_text(1, src);
    ExportOptions opts;
    auto res = export_markdown(out.document, opts);
    expect(fatal(bool(res.ok)));
    expect(fatal(bool((res.value.content) == (std::string("# Title\n\nHello *world*.\n")))));
    expect(fatal(bool((res.value.mime_type) == (std::string("text/markdown")))));
    expect(fatal(bool((res.value.extension) == (std::string("md")))));
};

"test_export_markdown_uses_authoritative_document"_test = [] {
    auto out = parse_text(1, "**alpha**");
    ExportOptions opts;
    auto result = export_markdown(out.document, opts);
    expect(fatal(bool(result.ok)));
    if (result.ok) expect(fatal(bool((result.value.content) == (std::string("**alpha**")))));
};

"test_export_plain_text_mime"_test = [] {
    auto res = export_plain_text("hello");
    expect(fatal(bool(res.ok)));
    expect(fatal(bool((res.value.content) == (std::string("hello")))));
    expect(fatal(bool((res.value.mime_type) == (std::string("text/plain")))));
};

"test_export_html_link_sanitizes_javascript"_test = [] {
    std::string src = "[click](JaVaScRiPt:evil)\n";
    auto out = parse_text(1, src);
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    expect(fatal(bool(res.ok)));
    expect(fatal(bool(res.value.content.find("href=\"javascript:") == std::string::npos)));
    expect(fatal(bool(res.value.content.find("JaVaScRiPt:") == std::string::npos)));
};

"test_export_html_only_allows_data_urls_for_images"_test = [] {
    auto out = parse_text(1, "![ok](data:image/png;base64,QQ==)\n\n![bad](data:text/html;base64,QQ==)\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    expect(fatal(bool(res.ok)));
    expect(fatal(bool(res.value.content.find("data:image/png") != std::string::npos)));
    expect(fatal(bool(res.value.content.find("data:text/html") == std::string::npos)));
};

"test_export_html_preserves_safe_image_dimensions"_test = [] {
    auto out = parse_text(1, "<img src=\"image.png\" width=\"320\" height=\"180\">\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    expect(fatal(bool(res.ok)));
    expect(fatal(bool(res.value.content.find("width=\"320.000000\"") != std::string::npos)));
    expect(fatal(bool(res.value.content.find("height=\"180.000000\"") != std::string::npos)));
};

"test_export_html_drop_policy"_test = [] {
    auto out = parse_text(1, "<iframe>x</iframe>\n");
    ExportOptions opts;
    opts.raw_html_policy = ExportRawHtmlPolicy::Drop;
    auto res = export_html("", out.document, opts);
    expect(fatal(bool(res.ok)));
    expect(fatal(bool(res.value.content.find("<iframe>") == std::string::npos)));
};

}; // suite export_tests

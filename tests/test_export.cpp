import std;
#include "test_framework.h"
import elmd.core.exporter;
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.settings;
import elmd.core.utf;

using namespace elmd;

ELMD_TEST(test_escape_raw_html_scripts) {
    std::string input = "<script>alert(1)</script>";
    std::string esc = escape_raw_html(input);
    ELMD_CHECK(esc.find("<script>") == std::string::npos);
    ELMD_CHECK(esc.find("&lt;script&gt;") != std::string::npos);
}

ELMD_TEST(test_export_html_escapes_script) {
    auto out = parse_text(1, "<script>alert(1)</script>\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    auto& html = res.value.content;
    ELMD_CHECK(html.find("<script>alert") == std::string::npos);
    ELMD_CHECK(html.find("&lt;script&gt;") != std::string::npos);
}

ELMD_TEST(test_export_html_escapes_div) {
    auto out = parse_text(1, "<div>hello</div>\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    auto& html = res.value.content;
    ELMD_CHECK(html.find("<div>hello</div>\n") == std::string::npos);
}

ELMD_TEST(test_export_markdown_serializes_document) {
    std::string src = "# Title\n\nHello *world*.\n";
    auto out = parse_text(1, src);
    ExportOptions opts;
    auto res = export_markdown(out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK_EQ(res.value.content, std::string("# Title\n\nHello *world*."));
    ELMD_CHECK_EQ(res.value.mime_type, std::string("text/markdown"));
    ELMD_CHECK_EQ(res.value.extension, std::string("md"));
}

ELMD_TEST(test_export_markdown_uses_authoritative_document) {
    auto out = parse_text(1, "**alpha**");
    ExportOptions opts;
    auto result = export_markdown(out.document, opts);
    ELMD_CHECK(result.ok);
    if (result.ok) ELMD_CHECK_EQ(result.value.content, std::string("**alpha**"));
}

ELMD_TEST(test_export_plain_text_mime) {
    auto res = export_plain_text("hello");
    ELMD_CHECK(res.ok);
    ELMD_CHECK_EQ(res.value.content, std::string("hello"));
    ELMD_CHECK_EQ(res.value.mime_type, std::string("text/plain"));
}

ELMD_TEST(test_export_html_link_sanitizes_javascript) {
    std::string src = "[click](JaVaScRiPt:evil)\n";
    auto out = parse_text(1, src);
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK(res.value.content.find("href=\"javascript:") == std::string::npos);
    ELMD_CHECK(res.value.content.find("JaVaScRiPt:") == std::string::npos);
}

ELMD_TEST(test_export_html_only_allows_data_urls_for_images) {
    auto out = parse_text(1, "![ok](data:image/png;base64,QQ==)\n\n![bad](data:text/html;base64,QQ==)\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK(res.value.content.find("data:image/png") != std::string::npos);
    ELMD_CHECK(res.value.content.find("data:text/html") == std::string::npos);
}

ELMD_TEST(test_export_html_preserves_safe_image_dimensions) {
    auto out = parse_text(1, "<img src=\"image.png\" width=\"320\" height=\"180\">\n");
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK(res.value.content.find("width=\"320.000000\"") != std::string::npos);
    ELMD_CHECK(res.value.content.find("height=\"180.000000\"") != std::string::npos);
}

ELMD_TEST(test_export_html_drop_policy) {
    auto out = parse_text(1, "<iframe>x</iframe>\n");
    ExportOptions opts;
    opts.raw_html_policy = ExportRawHtmlPolicy::Drop;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK(res.value.content.find("<iframe>") == std::string::npos);
}

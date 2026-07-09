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

ELMD_TEST(test_export_markdown_preserves_source) {
    std::string src = "# Title\n\nHello *world*.\n";
    auto out = parse_text(1, src);
    ExportOptions opts;
    auto res = export_markdown(src, out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK_EQ(res.value.content, src);
    ELMD_CHECK_EQ(res.value.mime_type, std::string("text/markdown"));
    ELMD_CHECK_EQ(res.value.extension, std::string("md"));
}

ELMD_TEST(test_export_plain_text_mime) {
    auto res = export_plain_text("hello");
    ELMD_CHECK(res.ok);
    ELMD_CHECK_EQ(res.value.content, std::string("hello"));
    ELMD_CHECK_EQ(res.value.mime_type, std::string("text/plain"));
}

ELMD_TEST(test_export_html_link_sanitizes_javascript) {
    std::string src = "[click](javascript:evil)\n";
    auto out = parse_text(1, src);
    ExportOptions opts;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK(res.value.content.find("href=\"javascript:") == std::string::npos);
}

ELMD_TEST(test_export_html_drop_policy) {
    auto out = parse_text(1, "<iframe>x</iframe>\n");
    ExportOptions opts;
    opts.raw_html_policy = ExportRawHtmlPolicy::Drop;
    auto res = export_html("", out.document, opts);
    ELMD_CHECK(res.ok);
    ELMD_CHECK(res.value.content.find("<iframe>") == std::string::npos);
}
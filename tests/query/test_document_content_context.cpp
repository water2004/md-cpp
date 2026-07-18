#include "support/folia_test.hpp"

import folia.core.document_content_context;
import folia.core.parser;
import folia.core.selection;
import folia.core.text_edit;

using namespace boost::ut;
using namespace folia;

suite document_content_context_tests = [] {

"content context distinguishes inline code math and ordinary source"_test = [] {
    auto document = parse_text(1, "plain `code` and $math$").document;
    auto const& paragraph = document.root.children.front();
    expect(document_content_context_at(document, {paragraph.id, 2, TextAffinity::Downstream})
        == DocumentContentContext::Normal);
    expect(document_content_context_at(document, {paragraph.id, 8, TextAffinity::Downstream})
        == DocumentContentContext::Code);
    expect(document_content_context_at(document, {paragraph.id, 19, TextAffinity::Downstream})
        == DocumentContentContext::Math);
};

"content context distinguishes fenced block owners"_test = [] {
    auto document = parse_text(1, "```cpp\ncode\n```\n\n$$\nx+y\n$$").document;
    expect(document.root.children.size() == 2_u);
    expect(document_content_context_at(
        document, {document.root.children[0].id, 0, TextAffinity::Downstream})
        == DocumentContentContext::Code);
    expect(document_content_context_at(
        document, {document.root.children[1].id, 0, TextAffinity::Downstream})
        == DocumentContentContext::Math);
};

};

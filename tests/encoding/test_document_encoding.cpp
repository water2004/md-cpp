#include "support/folia_test.hpp"

import folia.tests.document_encoding;

using namespace boost::ut;
using namespace folia::encoding_test;

suite document_encoding_tests = [] {

"UTF-8 BOM is preserved byte for byte"_test = [] {
    expect(utf8_bom_roundtrip());
};

"ICU candidates are ordered by confidence and unusable matches are skipped"_test = [] {
    expect(gb18030_candidates_and_roundtrip());
};

"explicit legacy encoding round trips and rejects lossy output"_test = [] {
    expect(legacy_roundtrip_rejects_loss());
};

"generic UTF-16 save resolves to concrete endian encoding"_test = [] {
    expect(utf16_resolution_roundtrip());
};

"ICU converter catalog is available"_test = [] {
    expect(converter_catalog_is_complete());
};

};

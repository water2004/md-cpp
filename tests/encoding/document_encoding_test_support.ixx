module;

#include "storage/DocumentEncodingService.h"

export module folia.tests.document_encoding;

namespace
{
    using namespace winrt::Folia;

    std::vector<std::uint8_t> Gb18030Fixture()
    {
        return {
            0xD5, 0xE2, 0xCA, 0xC7, 0xD2, 0xBB, 0xB8, 0xF6, 0xD3, 0xC3, 0xD3, 0xDA,
            0xB2, 0xE2, 0xCA, 0xD4, 0xD7, 0xD4, 0xB6, 0xAF, 0xB1, 0xE0, 0xC2, 0xEB,
            0xBC, 0xEC, 0xB2, 0xE2, 0xB5, 0xC4, 0xD6, 0xD0, 0xCE, 0xC4, 0x20, 0x4D,
            0x61, 0x72, 0x6B, 0x64, 0x6F, 0x77, 0x6E, 0x20, 0xCE, 0xC4, 0xB5, 0xB5,
            0xA1, 0xA3, 0xB1, 0xEA, 0xCC, 0xE2, 0xA1, 0xA2, 0xD5, 0xFD, 0xCE, 0xC4,
            0xBA, 0xCD, 0xC1, 0xD0, 0xB1, 0xED, 0xB6, 0xBC, 0xD3, 0xA6, 0xB8, 0xC3,
            0xD5, 0xFD, 0xC8, 0xB7, 0xCF, 0xD4, 0xCA, 0xBE, 0xA1, 0xA3, 0x0A,
        };
    }
}

export namespace folia::encoding_test
{
    bool utf8_bom_roundtrip()
    {
        auto bytes = std::vector<std::uint8_t>{0xEF, 0xBB, 0xBF, 'a', 'b', 'c'};
        auto decoded = DocumentEncodingService::Decode(bytes);
        return decoded.utf8 == "abc"
            && decoded.encoding.name == "UTF-8"
            && decoded.encoding.byteOrderMark == DocumentByteOrderMark::Utf8
            && DocumentEncodingService::Encode(decoded.utf8, decoded.encoding) == bytes;
    }

    bool gb18030_candidates_and_roundtrip()
    {
        auto bytes = Gb18030Fixture();
        auto decoded = DocumentEncodingService::Decode(bytes);
        if (decoded.encoding.name != "GB18030" || decoded.candidates.empty()) return false;
        for (std::size_t index = 1; index < decoded.candidates.size(); ++index)
            if (decoded.candidates[index - 1].confidence < decoded.candidates[index].confidence)
                return false;
        return DocumentEncodingService::Encode(decoded.utf8, decoded.encoding) == bytes;
    }

    bool legacy_roundtrip_rejects_loss()
    {
        auto bytes = std::vector<std::uint8_t>{'c', 'a', 'f', 0xE9};
        auto decoded = DocumentEncodingService::Decode(bytes, "windows-1252");
        if (decoded.utf8 != "caf\xC3\xA9"
            || DocumentEncodingService::Encode(decoded.utf8, decoded.encoding) != bytes)
            return false;
        try
        {
            (void)DocumentEncodingService::Encode("\xF0\x9F\x98\x80", decoded.encoding);
        }
        catch (DocumentEncodingError const&)
        {
            return true;
        }
        return false;
    }

    bool utf16_resolution_roundtrip()
    {
        auto encoding = DocumentEncoding{
            "UTF-16",
            DocumentByteOrderMark::Utf16LittleEndian,
            0,
            false,
        };
        encoding = DocumentEncodingService::NormalizeForSave(std::move(encoding));
        if (encoding.name != "UTF-16LE") return false;
        auto bytes = DocumentEncodingService::Encode("# \xE4\xB8\xAD\xE6\x96\x87\n", encoding);
        if (bytes.size() < 2 || bytes[0] != 0xFF || bytes[1] != 0xFE) return false;
        auto decoded = DocumentEncodingService::Decode(bytes);
        return decoded.encoding.name == "UTF-16LE"
            && decoded.utf8 == "# \xE4\xB8\xAD\xE6\x96\x87\n";
    }

    bool converter_catalog_is_complete()
    {
        auto available = DocumentEncodingService::AvailableEncodings();
        return available.size() > 100
            && std::find(available.begin(), available.end(), "GB18030") != available.end();
    }
}

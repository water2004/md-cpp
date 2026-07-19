#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::Folia
{
    enum class DocumentByteOrderMark
    {
        None,
        Utf8,
        Utf16LittleEndian,
        Utf16BigEndian,
        Utf32LittleEndian,
        Utf32BigEndian,
    };

    struct DocumentEncoding
    {
        std::string name = "UTF-8";
        DocumentByteOrderMark byteOrderMark = DocumentByteOrderMark::None;
        int confidence = 100;
        bool automaticallyDetected = false;
    };

    struct DocumentEncodingCandidate
    {
        std::string name;
        int confidence = 0;
    };

    struct DecodedDocument
    {
        std::string utf8;
        DocumentEncoding encoding;
        std::vector<DocumentEncodingCandidate> candidates;
    };

    class DocumentEncodingError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    struct DocumentEncodingService
    {
        static DecodedDocument Decode(
            std::span<std::uint8_t const> bytes,
            std::optional<std::string_view> requestedEncoding = std::nullopt);
        static std::vector<std::uint8_t> Encode(
            std::string_view utf8,
            DocumentEncoding const& encoding);
        static DocumentEncoding NormalizeForSave(DocumentEncoding encoding);
        static std::vector<std::string> AvailableEncodings();
        static std::vector<DocumentEncodingCandidate> DetectionCandidates(
            std::span<std::uint8_t const> bytes);
        static std::string CanonicalName(std::string_view name);
    };
}

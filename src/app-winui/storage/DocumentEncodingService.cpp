#include "storage/DocumentEncodingService.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>

#include <unicode/ucnv.h>
#include <unicode/ucnv_err.h>
#include <unicode/ucsdet.h>
#include <unicode/ustring.h>

namespace winrt::Folia
{
    namespace
    {
        constexpr std::size_t detectionSampleLimit = 512 * 1024;

        struct ConverterCloser
        {
            void operator()(UConverter* converter) const noexcept
            {
                if (converter) ucnv_close(converter);
            }
        };

        struct DetectorCloser
        {
            void operator()(UCharsetDetector* detector) const noexcept
            {
                if (detector) ucsdet_close(detector);
            }
        };

        using Converter = std::unique_ptr<UConverter, ConverterCloser>;
        using Detector = std::unique_ptr<UCharsetDetector, DetectorCloser>;

        struct BomMatch
        {
            DocumentByteOrderMark kind = DocumentByteOrderMark::None;
            std::string_view encoding;
            std::size_t size = 0;
        };

        [[noreturn]] void ThrowIcu(std::string_view operation, UErrorCode status)
        {
            throw DocumentEncodingError(
                std::string(operation) + " failed: " + u_errorName(status));
        }

        bool StartsWith(std::span<std::uint8_t const> bytes, std::initializer_list<std::uint8_t> prefix)
        {
            return bytes.size() >= prefix.size()
                && std::equal(prefix.begin(), prefix.end(), bytes.begin());
        }

        BomMatch DetectBom(std::span<std::uint8_t const> bytes)
        {
            if (StartsWith(bytes, { 0x00, 0x00, 0xFE, 0xFF }))
                return { DocumentByteOrderMark::Utf32BigEndian, "UTF-32BE", 4 };
            if (StartsWith(bytes, { 0xFF, 0xFE, 0x00, 0x00 }))
                return { DocumentByteOrderMark::Utf32LittleEndian, "UTF-32LE", 4 };
            if (StartsWith(bytes, { 0xEF, 0xBB, 0xBF }))
                return { DocumentByteOrderMark::Utf8, "UTF-8", 3 };
            if (StartsWith(bytes, { 0xFE, 0xFF }))
                return { DocumentByteOrderMark::Utf16BigEndian, "UTF-16BE", 2 };
            if (StartsWith(bytes, { 0xFF, 0xFE }))
                return { DocumentByteOrderMark::Utf16LittleEndian, "UTF-16LE", 2 };
            return {};
        }

        std::span<std::uint8_t const> RemoveBom(
            std::span<std::uint8_t const> bytes,
            BomMatch const& bom,
            std::string_view encoding)
        {
            if (bom.size == 0) return bytes;
            auto canonical = DocumentEncodingService::CanonicalName(encoding);
            auto bomCanonical = DocumentEncodingService::CanonicalName(bom.encoding);
            return canonical == bomCanonical ? bytes.subspan(bom.size) : bytes;
        }

        bool IsValidUtf8(std::span<std::uint8_t const> bytes)
        {
            std::size_t i = 0;
            while (i < bytes.size())
            {
                auto lead = bytes[i++];
                if (lead <= 0x7F) continue;
                std::size_t trailing = 0;
                std::uint32_t value = 0;
                std::uint32_t minimum = 0;
                if ((lead & 0xE0) == 0xC0)
                {
                    trailing = 1;
                    value = lead & 0x1F;
                    minimum = 0x80;
                }
                else if ((lead & 0xF0) == 0xE0)
                {
                    trailing = 2;
                    value = lead & 0x0F;
                    minimum = 0x800;
                }
                else if ((lead & 0xF8) == 0xF0)
                {
                    trailing = 3;
                    value = lead & 0x07;
                    minimum = 0x10000;
                }
                else
                {
                    return false;
                }
                if (i + trailing > bytes.size()) return false;
                for (std::size_t n = 0; n < trailing; ++n)
                {
                    auto next = bytes[i++];
                    if ((next & 0xC0) != 0x80) return false;
                    value = (value << 6) | (next & 0x3F);
                }
                if (value < minimum || value > 0x10FFFF
                    || (value >= 0xD800 && value <= 0xDFFF))
                    return false;
            }
            return true;
        }

        std::vector<std::uint8_t> DetectionSample(std::span<std::uint8_t const> bytes)
        {
            if (bytes.size() <= detectionSampleLimit)
                return { bytes.begin(), bytes.end() };
            auto half = detectionSampleLimit / 2;
            std::vector<std::uint8_t> sample;
            sample.reserve(detectionSampleLimit + 1);
            sample.insert(sample.end(), bytes.begin(), bytes.begin() + half);
            sample.push_back('\n');
            sample.insert(sample.end(), bytes.end() - half, bytes.end());
            return sample;
        }

        bool SameEncoding(std::string_view left, std::string_view right)
        {
            return _stricmp(std::string(left).c_str(), std::string(right).c_str()) == 0;
        }

        bool IsGenericUnicodeEncoding(std::string_view encoding, std::string_view family)
        {
            return SameEncoding(encoding, family);
        }

        std::string ConverterName(DocumentEncoding const& encoding)
        {
            if (IsGenericUnicodeEncoding(encoding.name, "UTF-16"))
            {
                if (encoding.byteOrderMark == DocumentByteOrderMark::Utf16LittleEndian)
                    return "UTF-16LE";
                if (encoding.byteOrderMark == DocumentByteOrderMark::Utf16BigEndian)
                    return "UTF-16BE";
            }
            if (IsGenericUnicodeEncoding(encoding.name, "UTF-32"))
            {
                if (encoding.byteOrderMark == DocumentByteOrderMark::Utf32LittleEndian)
                    return "UTF-32LE";
                if (encoding.byteOrderMark == DocumentByteOrderMark::Utf32BigEndian)
                    return "UTF-32BE";
            }
            return encoding.name;
        }

        std::vector<DocumentEncodingCandidate> DetectWithIcu(
            std::span<std::uint8_t const> bytes)
        {
            auto sample = DetectionSample(bytes);
            UErrorCode status = U_ZERO_ERROR;
            Detector detector{ ucsdet_open(&status) };
            if (U_FAILURE(status)) ThrowIcu("opening ICU charset detector", status);
            ucsdet_setText(
                detector.get(),
                reinterpret_cast<char const*>(sample.data()),
                static_cast<std::int32_t>(sample.size()),
                &status);
            if (U_FAILURE(status)) ThrowIcu("setting ICU charset detector input", status);
            std::int32_t matchCount = 0;
            auto matches = ucsdet_detectAll(detector.get(), &matchCount, &status);
            if (U_FAILURE(status) || !matches || matchCount == 0)
                ThrowIcu("detecting document encoding", status);

            std::vector<DocumentEncodingCandidate> result;
            result.reserve(static_cast<std::size_t>(matchCount));
            for (std::int32_t index = 0; index < matchCount; ++index)
            {
                status = U_ZERO_ERROR;
                auto name = ucsdet_getName(matches[index], &status);
                auto confidence = ucsdet_getConfidence(matches[index], &status);
                if (U_FAILURE(status) || !name) continue;
                std::string canonical;
                try
                {
                    canonical = DocumentEncodingService::CanonicalName(name);
                }
                catch (DocumentEncodingError const&)
                {
                    continue;
                }
                auto existing = std::find_if(result.begin(), result.end(), [&](auto const& candidate)
                {
                    return SameEncoding(candidate.name, canonical);
                });
                if (existing == result.end())
                    result.push_back({ std::move(canonical), confidence });
                else
                    existing->confidence = (std::max)(existing->confidence, confidence);
            }
            std::sort(result.begin(), result.end(), [](auto const& left, auto const& right)
            {
                if (left.confidence != right.confidence)
                    return left.confidence > right.confidence;
                return _stricmp(left.name.c_str(), right.name.c_str()) < 0;
            });
            if (result.empty())
                throw DocumentEncodingError("ICU did not return a usable document encoding");
            return result;
        }

        Converter OpenConverter(std::string_view name)
        {
            UErrorCode status = U_ZERO_ERROR;
            Converter converter{ ucnv_open(std::string(name).c_str(), &status) };
            if (U_FAILURE(status)) ThrowIcu("opening converter " + std::string(name), status);
            return converter;
        }

        std::string DecodeUtf8(std::span<std::uint8_t const> bytes, std::string_view encoding)
        {
            if (bytes.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
                throw DocumentEncodingError("document is too large for ICU conversion");
            auto converter = OpenConverter(encoding);
            UErrorCode status = U_ZERO_ERROR;
            ucnv_setToUCallBack(
                converter.get(), UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
            if (U_FAILURE(status)) ThrowIcu("configuring decoder", status);
            auto source = reinterpret_cast<char const*>(bytes.data());
            auto sourceLength = static_cast<std::int32_t>(bytes.size());
            auto utf16Length = ucnv_toUChars(converter.get(), nullptr, 0, source, sourceLength, &status);
            if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
                ThrowIcu("decoding " + std::string(encoding), status);
            status = U_ZERO_ERROR;
            std::vector<UChar> utf16(static_cast<std::size_t>(utf16Length) + 1);
            ucnv_resetToUnicode(converter.get());
            utf16Length = ucnv_toUChars(
                converter.get(), utf16.data(), static_cast<std::int32_t>(utf16.size()),
                source, sourceLength, &status);
            if (U_FAILURE(status)) ThrowIcu("decoding " + std::string(encoding), status);
            status = U_ZERO_ERROR;
            std::int32_t utf8Length = 0;
            u_strToUTF8(nullptr, 0, &utf8Length, utf16.data(), utf16Length, &status);
            if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
                ThrowIcu("measuring UTF-8 output", status);
            status = U_ZERO_ERROR;
            std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
            u_strToUTF8(
                utf8.data(), utf8Length, nullptr, utf16.data(), utf16Length, &status);
            if (U_FAILURE(status)) ThrowIcu("creating UTF-8 output", status);
            return utf8;
        }

        std::vector<UChar> Utf8ToUtf16(std::string_view utf8)
        {
            if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
                throw DocumentEncodingError("document is too large for ICU conversion");
            UErrorCode status = U_ZERO_ERROR;
            std::int32_t length = 0;
            u_strFromUTF8(
                nullptr, 0, &length, utf8.data(), static_cast<std::int32_t>(utf8.size()), &status);
            if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
                ThrowIcu("measuring UTF-16 input", status);
            status = U_ZERO_ERROR;
            std::vector<UChar> result(static_cast<std::size_t>(length) + 1);
            u_strFromUTF8(
                result.data(), static_cast<std::int32_t>(result.size()), &length,
                utf8.data(), static_cast<std::int32_t>(utf8.size()), &status);
            if (U_FAILURE(status)) ThrowIcu("decoding editor UTF-8", status);
            result.resize(static_cast<std::size_t>(length));
            return result;
        }

        std::span<std::uint8_t const> BomBytes(DocumentByteOrderMark bom)
        {
            static constexpr std::array<std::uint8_t, 3> utf8{ 0xEF, 0xBB, 0xBF };
            static constexpr std::array<std::uint8_t, 2> utf16le{ 0xFF, 0xFE };
            static constexpr std::array<std::uint8_t, 2> utf16be{ 0xFE, 0xFF };
            static constexpr std::array<std::uint8_t, 4> utf32le{ 0xFF, 0xFE, 0x00, 0x00 };
            static constexpr std::array<std::uint8_t, 4> utf32be{ 0x00, 0x00, 0xFE, 0xFF };
            switch (bom)
            {
                case DocumentByteOrderMark::Utf8: return utf8;
                case DocumentByteOrderMark::Utf16LittleEndian: return utf16le;
                case DocumentByteOrderMark::Utf16BigEndian: return utf16be;
                case DocumentByteOrderMark::Utf32LittleEndian: return utf32le;
                case DocumentByteOrderMark::Utf32BigEndian: return utf32be;
                case DocumentByteOrderMark::None: return {};
            }
            return {};
        }
    }

    std::string DocumentEncodingService::CanonicalName(std::string_view name)
    {
        if (name.empty()) throw DocumentEncodingError("encoding name is empty");
        auto converter = OpenConverter(name);
        UErrorCode status = U_ZERO_ERROR;
        auto canonical = ucnv_getStandardName(std::string(name).c_str(), "IANA", &status);
        if (U_SUCCESS(status) && canonical) return canonical;
        status = U_ZERO_ERROR;
        auto internal = ucnv_getName(converter.get(), &status);
        if (U_FAILURE(status) || !internal) ThrowIcu("reading converter name", status);
        return internal;
    }

    DecodedDocument DocumentEncodingService::Decode(
        std::span<std::uint8_t const> bytes,
        std::optional<std::string_view> requestedEncoding)
    {
        auto bom = DetectBom(bytes);
        auto candidates = DetectionCandidates(bytes);
        DocumentEncoding encoding;
        if (requestedEncoding)
        {
            encoding.name = CanonicalName(*requestedEncoding);
            if (bom.size != 0
                && ((IsGenericUnicodeEncoding(encoding.name, "UTF-16")
                        && (bom.kind == DocumentByteOrderMark::Utf16LittleEndian
                            || bom.kind == DocumentByteOrderMark::Utf16BigEndian))
                    || (IsGenericUnicodeEncoding(encoding.name, "UTF-32")
                        && (bom.kind == DocumentByteOrderMark::Utf32LittleEndian
                            || bom.kind == DocumentByteOrderMark::Utf32BigEndian))))
                encoding.name = CanonicalName(bom.encoding);
            auto match = std::find_if(candidates.begin(), candidates.end(), [&](auto const& candidate)
            {
                return SameEncoding(candidate.name, encoding.name);
            });
            encoding.confidence = match == candidates.end() ? 0 : match->confidence;
            encoding.automaticallyDetected = false;
        }
        else if (bom.size != 0)
        {
            encoding.name = CanonicalName(bom.encoding);
            encoding.confidence = 100;
            encoding.automaticallyDetected = true;
        }
        else
        {
            encoding.name = candidates.front().name;
            encoding.confidence = candidates.front().confidence;
            encoding.automaticallyDetected = true;
        }
        auto source = RemoveBom(bytes, bom, encoding.name);
        if (source.size() != bytes.size()) encoding.byteOrderMark = bom.kind;
        return {
            DecodeUtf8(source, encoding.name),
            std::move(encoding),
            std::move(candidates),
        };
    }

    std::vector<std::uint8_t> DocumentEncodingService::Encode(
        std::string_view utf8,
        DocumentEncoding const& encoding)
    {
        auto utf16 = Utf8ToUtf16(utf8);
        auto converterName = ConverterName(encoding);
        auto converter = OpenConverter(converterName);
        UErrorCode status = U_ZERO_ERROR;
        ucnv_setFromUCallBack(
            converter.get(), UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
        if (U_FAILURE(status)) ThrowIcu("configuring encoder", status);
        auto length = ucnv_fromUChars(
            converter.get(), nullptr, 0, utf16.data(), static_cast<std::int32_t>(utf16.size()), &status);
        if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
            ThrowIcu("encoding " + converterName, status);
        status = U_ZERO_ERROR;
        auto bom = BomBytes(encoding.byteOrderMark);
        std::vector<std::uint8_t> result(bom.begin(), bom.end());
        auto offset = result.size();
        result.resize(offset + static_cast<std::size_t>(length));
        ucnv_resetFromUnicode(converter.get());
        ucnv_fromUChars(
            converter.get(), reinterpret_cast<char*>(result.data() + offset), length,
            utf16.data(), static_cast<std::int32_t>(utf16.size()), &status);
        if (U_FAILURE(status)) ThrowIcu("encoding " + converterName, status);
        return result;
    }

    DocumentEncoding DocumentEncodingService::NormalizeForSave(DocumentEncoding encoding)
    {
        encoding.name = CanonicalName(encoding.name);
        encoding.name = CanonicalName(ConverterName(encoding));
        encoding.confidence = 100;
        encoding.automaticallyDetected = false;
        return encoding;
    }

    std::vector<std::string> DocumentEncodingService::AvailableEncodings()
    {
        std::vector<std::string> result;
        auto count = ucnv_countAvailable();
        result.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index)
        {
            auto name = ucnv_getAvailableName(index);
            if (!name) continue;
            try
            {
                result.push_back(CanonicalName(name));
            }
            catch (DocumentEncodingError const&)
            {
            }
        }
        std::sort(result.begin(), result.end(), [](auto const& left, auto const& right)
        {
            return _stricmp(left.c_str(), right.c_str()) < 0;
        });
        result.erase(std::unique(result.begin(), result.end(), [](auto const& left, auto const& right)
        {
            return _stricmp(left.c_str(), right.c_str()) == 0;
        }), result.end());
        return result;
    }

    std::vector<DocumentEncodingCandidate> DocumentEncodingService::DetectionCandidates(
        std::span<std::uint8_t const> bytes)
    {
        auto bom = DetectBom(bytes);
        if (bom.size != 0)
            return { { CanonicalName(bom.encoding), 100 } };
        if (bytes.empty() || IsValidUtf8(bytes))
            return { { "UTF-8", 100 } };
        return DetectWithIcu(bytes);
    }
}

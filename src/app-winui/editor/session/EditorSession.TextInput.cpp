#include "pch.h"
#include "editor/session/EditorSession.h"

import folia.core.document_text;

namespace winrt::Folia
{
    namespace
    {
        std::wstring BoundaryWide(std::u32string_view text)
        {
            std::wstring result;
            for (auto codepoint : text)
            {
                if (codepoint <= 0xffff)
                {
                    result.push_back(static_cast<wchar_t>(codepoint));
                }
                else
                {
                    codepoint -= 0x10000;
                    result.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
                    result.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
                }
            }
            return result;
        }

        std::size_t Utf16OffsetForCodepoint(
            std::u32string_view text,
            std::size_t codepointOffset)
        {
            codepointOffset = (std::min)(codepointOffset, text.size());
            std::size_t result = 0;
            for (std::size_t index = 0; index < codepointOffset; ++index)
                result += text[index] > 0xffff ? 2u : 1u;
            return result;
        }

        std::size_t CodepointOffsetForUtf16(
            std::u32string_view text,
            std::size_t utf16Offset)
        {
            std::size_t consumed = 0;
            std::size_t result = 0;
            while (result < text.size())
            {
                auto const units = text[result] > 0xffff ? 2u : 1u;
                if (consumed + units > utf16Offset) break;
                consumed += units;
                ++result;
            }
            return result;
        }
    }

    std::wstring EditorSession::TextInputTextUtf16(folia::NodeId containerId) const
    {
        auto source = TextInputSourceView(containerId);
        return source ? BoundaryWide(*source) : std::wstring{};
    }

    std::size_t EditorSession::TextInputAcpOffset(folia::TextPosition position) const
    {
        auto source = TextInputSourceView(position.container_id);
        if (!source) return 0;
        auto const offset = (std::min)(position.source_offset, source->size());
        return Utf16OffsetForCodepoint(*source, offset);
    }

    folia::TextPosition EditorSession::TextInputPositionFromAcp(
        folia::NodeId containerId,
        std::size_t offset,
        folia::TextAffinity affinity) const
    {
        auto source = TextInputSourceView(containerId);
        if (!source) return {};
        auto localOffset = CodepointOffsetForUtf16(*source, offset);
        return {containerId, (std::min)(localOffset, source->size()), affinity};
    }

    std::optional<std::u32string_view> EditorSession::TextInputSourceView(
        folia::NodeId containerId) const
    {
        if (core_->sourceEditor)
        {
            auto found = std::ranges::find(
                core_->sourceEditor->lines(), containerId, &folia::SourceLine::id);
            if (found == core_->sourceEditor->lines().end()) return std::nullopt;
            return std::u32string_view{found->text};
        }
        return folia::document_editable_text_view(core_->editor.document(), containerId);
    }

    std::optional<std::u32string> EditorSession::EditableSource(folia::NodeId id) const
    {
        if (core_->sourceEditor)
        {
            auto found = std::ranges::find(core_->sourceEditor->lines(), id, &folia::SourceLine::id);
            if (found == core_->sourceEditor->lines().end()) return std::nullopt;
            return found->text;
        }
        return core_->editor.editable_source(id);
    }
}

#pragma once

import elmd.core.text_edit;

namespace winrt::ElMd
{
    struct EditorVisualBlock
    {
        D2D1_RECT_F rect{};
        D2D1_POINT_2F textOrigin{};
        float textWidth = 0.0f;
        elmd::TextSpan sourceSpan;
        float documentY = 0.0f;
        std::u32string text;
        std::vector<elmd::TextPosition> displayToSource;
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        bool thematicBreak = false;
    };

    struct EditorVisualLine
    {
        std::size_t blockIndex = 0;
        std::size_t tableIndex = (std::numeric_limits<std::size_t>::max)();
        std::size_t cellIndex = (std::numeric_limits<std::size_t>::max)();
        elmd::TextSpan sourceSpan;
        std::uint32_t displayStart = 0;
        std::uint32_t displayEnd = 0;
        bool wrapContinuation = false;
        D2D1_RECT_F rect{};
    };

    struct EditorVisualTableCell
    {
        D2D1_RECT_F rect{};
        D2D1_POINT_2F textOrigin{};
        float textWidth = 0.0f;
        float textHeight = 0.0f;
        elmd::TextSpan sourceSpan;
        std::size_t row = 0;
        std::size_t column = 0;
        std::u32string text;
        std::vector<elmd::TextPosition> displayToSource;
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    };

    struct EditorVisualTable
    {
        D2D1_RECT_F rect{};
        std::vector<elmd::TextSpan> sourceSpans;
        std::size_t rowCount = 0;
        std::size_t columnCount = 0;
        bool editable = true;
        std::vector<float> rowBoundaries;
        std::vector<float> columnBoundaries;
        std::vector<EditorVisualTableCell> cells;
    };

    struct EditorVisualMathHit
    {
        D2D1_RECT_F rect{};
        elmd::TextSpan sourceSpan;
        float progressStart = 0.0f;
        float progressEnd = 1.0f;
    };

    struct EditorCaretMove
    {
        elmd::TextPosition position;
        bool upstream = false;
    };

    struct EditorInteractionMap
    {
        void Clear(std::size_t blockCapacity);
        void AddBlockLines(std::size_t blockIndex);
        void AddTableCellLines(std::size_t blockIndex, std::size_t tableIndex, std::size_t cellIndex);
        std::optional<elmd::TextPosition> HitTest(float x, float y, bool* outUpstream = nullptr) const;
        std::optional<D2D1_RECT_F> CaretBounds(elmd::TextPosition position, bool upstream, float bodyLineHeight) const;
        std::optional<EditorCaretMove> MoveCaretVertically(elmd::TextPosition position, bool upstream, bool down, float& goalX, float bodyLineHeight) const;
        std::optional<elmd::TextPosition> VisualLineStart(elmd::TextPosition position, bool upstream) const;
        std::optional<elmd::TextPosition> VisualLineEnd(elmd::TextPosition position, bool upstream) const;

        std::vector<EditorVisualBlock> blocks;
        std::vector<EditorVisualLine> lines;
        std::vector<EditorVisualTable> tables;
        std::vector<EditorVisualMathHit> mathHits;

    private:
        std::optional<std::size_t> LineIndexFor(elmd::TextPosition position, bool upstream) const;
        std::optional<D2D1_RECT_F> CaretRectOnLine(EditorVisualLine const& line, elmd::TextPosition position, bool upstream, float bodyLineHeight) const;
    };
}

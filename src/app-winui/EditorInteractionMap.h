#pragma once

namespace winrt::ElMd
{
    struct EditorVisualBlock
    {
        D2D1_RECT_F rect{};
        D2D1_POINT_2F textOrigin{};
        float textWidth = 0.0f;
        std::size_t sourceStart = 0;
        std::size_t sourceEnd = 0;
        float documentY = 0.0f;
        std::u32string text;
        std::vector<std::size_t> displayToSource;
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        bool thematicBreak = false;
    };

    struct EditorVisualLine
    {
        std::size_t blockIndex = 0;
        std::size_t tableIndex = (std::numeric_limits<std::size_t>::max)();
        std::size_t cellIndex = (std::numeric_limits<std::size_t>::max)();
        std::size_t sourceStart = 0;
        std::size_t sourceEnd = 0;
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
        std::size_t sourceStart = 0;
        std::size_t sourceEnd = 0;
        std::size_t row = 0;
        std::size_t column = 0;
        std::u32string text;
        std::vector<std::size_t> displayToSource;
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    };

    struct EditorVisualTable
    {
        D2D1_RECT_F rect{};
        std::size_t sourceStart = 0;
        std::size_t sourceEnd = 0;
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
        std::size_t sourceStart = 0;
        std::size_t sourceEnd = 0;
        float progressStart = 0.0f;
        float progressEnd = 1.0f;
    };

    struct EditorCaretMove
    {
        std::size_t offset = 0;
        bool upstream = false;
    };

    struct EditorInteractionMap
    {
        void Clear(std::size_t blockCapacity);
        void AddBlockLines(std::size_t blockIndex);
        void AddTableCellLines(std::size_t blockIndex, std::size_t tableIndex, std::size_t cellIndex);
        std::optional<std::size_t> HitTest(float x, float y, bool* outUpstream = nullptr) const;
        std::optional<D2D1_RECT_F> CaretBounds(std::size_t sourceOffset, bool upstream, float bodyLineHeight) const;
        std::optional<EditorCaretMove> MoveCaretVertically(std::size_t sourceOffset, bool upstream, bool down, float& goalX, float bodyLineHeight) const;
        std::optional<std::size_t> VisualLineStart(std::size_t sourceOffset, bool upstream) const;
        std::optional<std::size_t> VisualLineEnd(std::size_t sourceOffset, bool upstream) const;

        std::vector<EditorVisualBlock> blocks;
        std::vector<EditorVisualLine> lines;
        std::vector<EditorVisualTable> tables;
        std::vector<EditorVisualMathHit> mathHits;

    private:
        std::optional<std::size_t> LineIndexFor(std::size_t sourceOffset, bool upstream) const;
        std::optional<D2D1_RECT_F> CaretRectOnLine(EditorVisualLine const& line, std::size_t sourceOffset, bool upstream, float bodyLineHeight) const;
    };
}

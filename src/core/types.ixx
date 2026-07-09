// elmd.core.types — strong-typed offsets, ranges, geometry. Platform-agnostic.
export module elmd.core.types;
import std;

export namespace elmd {

// ---------------------------------------------------------------------------
// Strong-typed offsets. These must NEVER mix implicitly — the project's
// correctness rests on the char/byte/utf16/grapheme distinction.
// ---------------------------------------------------------------------------
struct ByteOffset {
    std::size_t v{};
    constexpr ByteOffset() = default;
    constexpr explicit ByteOffset(std::size_t x) : v(x) {}
    constexpr static ByteOffset ZERO() { return ByteOffset(0); }
    constexpr bool operator==(const ByteOffset& o) const { return v == o.v; }
    constexpr bool operator!=(const ByteOffset& o) const { return v != o.v; }
    constexpr bool operator<(const ByteOffset& o) const { return v < o.v; }
    constexpr bool operator<=(const ByteOffset& o) const { return v <= o.v; }
    constexpr bool operator>(const ByteOffset& o) const { return v > o.v; }
    constexpr bool operator>=(const ByteOffset& o) const { return v >= o.v; }
};

struct CharOffset {
    std::size_t v{};
    constexpr CharOffset() = default;
    constexpr explicit CharOffset(std::size_t x) : v(x) {}
    constexpr static CharOffset ZERO() { return CharOffset(0); }
    // CharOffset + n; saturating - n (mirrors Rust saturating_sub semantics).
    constexpr CharOffset operator+(std::size_t n) const { return CharOffset(v + n); }
    constexpr CharOffset operator-(std::size_t n) const { return CharOffset(n >= v ? 0 : v - n); }
    constexpr CharOffset& operator+=(std::size_t n) { v += n; return *this; }
    constexpr bool operator==(const CharOffset& o) const { return v == o.v; }
    constexpr bool operator!=(const CharOffset& o) const { return v != o.v; }
    constexpr bool operator<(const CharOffset& o) const { return v < o.v; }
    constexpr bool operator<=(const CharOffset& o) const { return v <= o.v; }
    constexpr bool operator>(const CharOffset& o) const { return v > o.v; }
    constexpr bool operator>=(const CharOffset& o) const { return v >= o.v; }
};

struct Utf16Offset {
    std::size_t v{};
    constexpr Utf16Offset() = default;
    constexpr explicit Utf16Offset(std::size_t x) : v(x) {}
    constexpr static Utf16Offset ZERO() { return Utf16Offset(0); }
    constexpr bool operator==(const Utf16Offset& o) const { return v == o.v; }
    constexpr bool operator!=(const Utf16Offset& o) const { return v != o.v; }
    constexpr bool operator<(const Utf16Offset& o) const { return v < o.v; }
    constexpr bool operator<=(const Utf16Offset& o) const { return v <= o.v; }
};

struct GraphemeOffset {
    std::size_t v{};
    constexpr GraphemeOffset() = default;
    constexpr explicit GraphemeOffset(std::size_t x) : v(x) {}
    constexpr static GraphemeOffset ZERO() { return GraphemeOffset(0); }
    constexpr bool operator==(const GraphemeOffset& o) const { return v == o.v; }
    constexpr bool operator!=(const GraphemeOffset& o) const { return v != o.v; }
};

struct LineCol {
    std::size_t line{};
    std::size_t column{};
};

// TextRange<T> — half-open [start, end)
template <typename T>
struct TextRange {
    T start{};
    T end{};
    TextRange() = default;
    TextRange(T s, T e) : start(s), end(e) { if (end.v < start.v) end = start; }
    bool is_empty() const { return start.v == end.v; }
    std::size_t len() const { return end.v >= start.v ? end.v - start.v : 0; }
    std::size_t char_len() const { return len(); }
    std::size_t utf16_len() const { return len(); }
    bool contains(T point) const { return start <= point && point < end; }
};

using CharRange = TextRange<CharOffset>;

// ---------------------------------------------------------------------------
// Geometry — logical device-independent pixels (pre-DPI-scale).
// ---------------------------------------------------------------------------
struct LogicalPoint {
    float x{};
    float y{};
    LogicalPoint() = default;
    LogicalPoint(float x_, float y_) : x(x_), y(y_) {}
};

struct LogicalSize {
    float width{};
    float height{};
    LogicalSize() = default;
    LogicalSize(float w, float h) : width(w), height(h) {}
};

struct LogicalRect {
    float x{};
    float y{};
    float width{};
    float height{};
    LogicalRect() = default;
    LogicalRect(float x_, float y_, float w_, float h_)
        : x(x_), y(y_), width(w_), height(h_) {}
    float top() const { return y; }
    float bottom() const { return y + height; }
    float left() const { return x; }
    float right() const { return x + width; }
    bool contains_point(LogicalPoint p) const {
        return p.x >= x && p.x < x + width && p.y >= y && p.y < y + height;
    }
};

} // namespace elmd
// folia.core.types — platform-independent geometry and rendering primitives.
export module folia.core.types;
import std;

export namespace folia {

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

} // namespace folia

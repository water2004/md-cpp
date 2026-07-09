// elmd.platform.native_editor_surface — platform boundary for the self-drawn editor.
// This interface intentionally exposes core geometry/model types only. Concrete
// WinUI 3 / SwapChainPanel / Direct2D implementations live behind this layer.
export module elmd.platform.native_editor_surface;
import std;
import elmd.core.types;
import elmd.core.theme;
import elmd.core.ids;
import elmd.core.layout_tree;
import elmd.core.hit_test;
import elmd.core.selection_geometry;
import elmd.core.snapshot;

export namespace elmd::platform {

struct PaintCommand {
    enum class Kind { FillRect, StrokeRect, DrawGlyphRun, DrawLine, DrawImage };
    Kind kind = Kind::FillRect;
    elmd::LogicalRect rect;
    elmd::LogicalPoint from;
    elmd::LogicalPoint to;
    elmd::GlyphRunLayout run;
    elmd::BrushId brush;
    elmd::ImageId image;
    float width = 1.0f;
};

struct PaintContext {
    elmd::LogicalRect clip_rect;
    std::vector<PaintCommand> commands;

    explicit PaintContext(elmd::LogicalRect clip = {}) : clip_rect(clip) {}
};

class NativeEditorSurface {
public:
    virtual ~NativeEditorSurface() = default;
    virtual void set_viewport(elmd::LogicalRect viewport) = 0;
    virtual void set_scale_factor(float scale_factor) = 0;
    virtual void set_theme(elmd::Theme theme) = 0;
    virtual void update_snapshot(std::shared_ptr<const elmd::EditorSnapshot> snapshot) = 0;
    virtual void request_repaint(std::optional<elmd::LogicalRect> dirty) = 0;
    virtual bool paint(PaintContext& ctx) = 0;
    virtual std::optional<elmd::HitTestResult> hit_test(elmd::LogicalPoint point) const = 0;
    virtual std::optional<elmd::LogicalRect> caret_rect(elmd::CharOffset position) const = 0;
    virtual void scroll_to(elmd::CharOffset position) = 0;
};

} // namespace elmd::platform

// elmd.core.hit_test — hit_test_layout_tree.
export module elmd.core.hit_test;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.layout_tree;
import elmd.core.selection;
import elmd.core.text_edit;

export namespace elmd {

enum class HitKind { Text, Marker, Embedded, LineEnd, BlockGap };

struct HitTestResult {
    TextPosition position{};
    HitKind hit_kind = HitKind::Text;
};

inline std::optional<HitTestResult> hit_test_line(const TextLineLayout& line, LogicalPoint point, NodeId node_id) {
    for (const auto& run : line.runs) {
        float sx = run.origin.x, ex = run.origin.x + run.width;
        if (point.x >= sx && point.x < ex) {
            if (run.generated_boundary_affinity) {
                return HitTestResult{
                    TextPosition{
                        run.source_span.container_id,
                        run.source_span.source_range.start,
                        *run.generated_boundary_affinity},
                    HitKind::Marker};
            }
            float off = point.x - sx;
            float acc = 0;
            std::size_t char_index = run.glyphs.size();
            auto affinity = TextAffinity::Upstream;
            for (std::size_t i = 0; i < run.glyphs.size(); ++i) {
                if (off < acc + run.glyphs[i].advance) {
                    float mid = acc + run.glyphs[i].advance * 0.5f;
                    const bool before_glyph = off < mid;
                    char_index = before_glyph ? i : i + 1;
                    affinity = before_glyph ? TextAffinity::Downstream : TextAffinity::Upstream;
                    break;
                }
                acc += run.glyphs[i].advance;
            }
            std::size_t pos = run.source_span.source_range.start + std::min(char_index, run.glyphs.size());
            HitTestResult hr;
            hr.position = TextPosition{run.source_span.container_id, pos, affinity};
            hr.hit_kind = (run.marker_visibility == MarkerVisibility::HiddenButEditable) ? HitKind::Marker : HitKind::Text;
            return hr;
        }
    }
    const auto span = line.runs.empty() ? line.source_span : line.runs.back().source_span;
    HitTestResult hr;
    hr.position = TextPosition{span.container_id, span.source_range.end, TextAffinity::Upstream};
    hr.hit_kind = HitKind::LineEnd;
    return hr;
}

inline std::optional<HitTestResult> hit_test_block(const LayoutBlock& b, LogicalPoint point, std::optional<HitTestResult>& embedded_hit) {
    for (const auto& ch : b.children) {
        if (ch.kind == LayoutItem::Kind::Line && ch.line.rect.contains_point(point)) {
            return hit_test_line(ch.line, point, b.id);
        }
        if (ch.kind == LayoutItem::Kind::Embedded && ch.embedded.rect.contains_point(point)) {
            embedded_hit = HitTestResult{TextPosition{ch.embedded.source_span.container_id,
                ch.embedded.source_span.source_range.start, TextAffinity::Downstream}, HitKind::Embedded};
        }
    }
    return std::nullopt;
}

inline std::optional<HitTestResult> hit_test_layout_tree(const LayoutTree& tree, LogicalPoint point) {
    std::optional<HitTestResult> embedded_hit;
    for (const auto& b : tree.blocks) {
        if (b.rect.contains_point(point)) {
            auto r = hit_test_block(b, point, embedded_hit);
            if (r) return r;
            return HitTestResult{TextPosition{b.id, 0, TextAffinity::Downstream}, HitKind::BlockGap};
        }
    }
    if (const auto* b = tree.block_at_y(point.y)) {
        auto r = hit_test_block(*b, point, embedded_hit);
        if (r) return r;
        return HitTestResult{TextPosition{b->id, 0, TextAffinity::Downstream}, HitKind::BlockGap};
    }
    return std::nullopt;
}

} // namespace elmd

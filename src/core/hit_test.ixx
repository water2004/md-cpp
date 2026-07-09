// elmd.core.hit_test — hit_test_layout_tree.
export module elmd.core.hit_test;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.layout_tree;
import elmd.core.selection;

export namespace elmd {

enum class HitKind { Text, Marker, Embedded, LineEnd, BlockGap };

struct HitTestResult {
    CharOffset position{};
    std::optional<NodeId> node_id;
    TextAffinity affinity = TextAffinity::Downstream;
    HitKind hit_kind = HitKind::Text;
};

inline std::optional<HitTestResult> hit_test_line(const TextLineLayout& line, LogicalPoint point, NodeId node_id) {
    for (const auto& run : line.runs) {
        float sx = run.origin.x, ex = run.origin.x + run.width;
        if (point.x >= sx && point.x < ex) {
            float off = point.x - sx;
            float acc = 0;
            std::size_t char_index = run.glyphs.size();
            for (std::size_t i = 0; i < run.glyphs.size(); ++i) {
                if (off < acc + run.glyphs[i].advance) {
                    float mid = acc + run.glyphs[i].advance * 0.5f;
                    char_index = (off < mid) ? i : i + 1;
                    break;
                }
                acc += run.glyphs[i].advance;
            }
            std::size_t pos = run.source_range.start.v + std::min(char_index, run.glyphs.size());
            HitTestResult hr;
            hr.position = CharOffset(pos);
            hr.node_id = node_id;
            hr.affinity = (point.x > (sx + run.width * 0.5f)) ? TextAffinity::Downstream : TextAffinity::Upstream;
            hr.hit_kind = (run.marker_visibility == MarkerVisibility::HiddenButEditable) ? HitKind::Marker : HitKind::Text;
            return hr;
        }
    }
    std::size_t end_pos = line.runs.empty() ? line.source_range.start.v : line.runs.back().source_range.end.v;
    HitTestResult hr;
    hr.position = CharOffset(end_pos);
    hr.affinity = TextAffinity::Downstream;
    hr.hit_kind = HitKind::LineEnd;
    return hr;
}

inline std::optional<HitTestResult> hit_test_block(const LayoutBlock& b, LogicalPoint point, std::optional<HitTestResult>& embedded_hit) {
    for (const auto& ch : b.children) {
        if (ch.kind == LayoutItem::Kind::Line && ch.line.rect.contains_point(point)) {
            return hit_test_line(ch.line, point, b.id);
        }
        if (ch.kind == LayoutItem::Kind::Embedded && ch.embedded.rect.contains_point(point)) {
            embedded_hit = HitTestResult{b.source_range.start, b.id, TextAffinity::Downstream, HitKind::Embedded};
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
            return HitTestResult{b.source_range.start, b.id, TextAffinity::Downstream, HitKind::BlockGap};
        }
    }
    if (const auto* b = tree.block_at_y(point.y)) {
        auto r = hit_test_block(*b, point, embedded_hit);
        if (r) return r;
        return HitTestResult{b->source_range.start, b->id, TextAffinity::Downstream, HitKind::BlockGap};
    }
    return std::nullopt;
}

} // namespace elmd
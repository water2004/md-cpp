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

inline std::optional<HitTestResult> hit_test_block(const LayoutBlock& b, LogicalPoint point) {
    for (const auto& ch : b.children) {
        if (ch.kind == LayoutItem::Kind::Line && ch.line.rect.contains_point(point)) {
            return hit_test_line(ch.line, point, b.id);
        }
        if (ch.kind == LayoutItem::Kind::Embedded && ch.embedded.rect.contains_point(point)) {
            return HitTestResult{TextPosition{ch.embedded.source_span.container_id,
                ch.embedded.source_span.source_range.start, TextAffinity::Downstream}, HitKind::Embedded};
        }
        if (ch.kind == LayoutItem::Kind::Table && ch.table.rect.contains_point(point)) {
            for (const auto& row : ch.table.rows) {
                for (const auto& cell : row.cells) {
                    if (!cell.rect.contains_point(point)) continue;
                    for (const auto& content : cell.content) {
                        if (content.kind == LayoutItem::Kind::Line && content.line.rect.contains_point(point)) {
                            return hit_test_line(content.line, point, cell.source_span.container_id);
                        }
                    }
                    return HitTestResult{TextPosition{
                        cell.source_span.container_id,
                        cell.source_span.source_range.start,
                        TextAffinity::Downstream}, HitKind::BlockGap};
                }
            }
        }
        if (ch.kind == LayoutItem::Kind::BlockMath && ch.math.rect.contains_point(point)) {
            return HitTestResult{TextPosition{
                ch.math.source_span.container_id,
                ch.math.source_span.source_range.start,
                TextAffinity::Downstream}, HitKind::Embedded};
        }
        if (ch.kind == LayoutItem::Kind::Image && ch.image.rect.contains_point(point)) {
            return HitTestResult{TextPosition{
                ch.image.source_span.container_id,
                ch.image.source_span.source_range.start,
                TextAffinity::Downstream}, HitKind::Embedded};
        }
    }
    return std::nullopt;
}

inline std::optional<TextPosition> block_boundary_position(const LayoutBlock& block, bool at_end) {
    std::vector<TextSpan> spans;
    auto append = [&](TextSpan span) {
        if (span.container_id.v != 0) spans.push_back(span);
    };
    for (const auto& child : block.children) {
        if (child.kind == LayoutItem::Kind::Line) {
            append(child.line.source_span);
        } else if (child.kind == LayoutItem::Kind::Embedded) {
            append(child.embedded.source_span);
        } else if (child.kind == LayoutItem::Kind::Table) {
            for (const auto& row : child.table.rows) {
                for (const auto& cell : row.cells) append(cell.source_span);
            }
        } else if (child.kind == LayoutItem::Kind::BlockMath) {
            append(child.math.source_span);
        } else if (child.kind == LayoutItem::Kind::Image) {
            append(child.image.source_span);
        }
    }
    if (spans.empty()) append(block.source_span);
    if (spans.empty()) return std::nullopt;
    const auto& span = at_end ? spans.back() : spans.front();
    return TextPosition{
        span.container_id,
        at_end ? span.source_range.end : span.source_range.start,
        at_end ? TextAffinity::Upstream : TextAffinity::Downstream};
}

inline std::optional<HitTestResult> hit_test_layout_tree(const LayoutTree& tree, LogicalPoint point) {
    for (const auto& b : tree.blocks) {
        if (b.rect.contains_point(point)) {
            auto r = hit_test_block(b, point);
            if (r) return r;
            auto boundary = block_boundary_position(
                b,
                point.y >= b.rect.y + b.rect.height * 0.5f);
            if (boundary) return HitTestResult{*boundary, HitKind::BlockGap};
            return std::nullopt;
        }
    }
    if (const auto* b = tree.block_at_y(point.y)) {
        auto r = hit_test_block(*b, point);
        if (r) return r;
        auto boundary = block_boundary_position(
            *b,
            point.y >= b->rect.y + b->rect.height * 0.5f);
        if (boundary) return HitTestResult{*boundary, HitKind::BlockGap};
    }
    return std::nullopt;
}

} // namespace elmd

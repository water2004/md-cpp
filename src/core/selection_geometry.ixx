// elmd.core.selection_geometry — selection / caret geometry from the layout.
export module elmd.core.selection_geometry;
import std;
import elmd.core.types;
import elmd.core.layout_tree;
import elmd.core.selection;

export namespace elmd {

struct SelectionRect {
    TextRange<CharOffset> source_range;
    LogicalRect rect;
};

struct SelectionGeometry {
    std::uint64_t revision{};
    std::vector<SelectionRect> ranges;
};

inline SelectionGeometry compute_selection_geometry(const LayoutTree& tree, TextRange<CharOffset> selection) {
    SelectionGeometry sg;
    sg.revision = tree.revision;
    for (const auto& b : tree.blocks) {
        for (const auto& ch : b.children) if (ch.kind == LayoutItem::Kind::Line) {
            for (const auto& run : ch.line.runs) {
                if (run.source_range.end.v <= selection.start.v || run.source_range.start.v >= selection.end.v) continue;
                std::size_t sel_start = std::max(run.source_range.start.v, selection.start.v);
                std::size_t sel_end = std::min(run.source_range.end.v, selection.end.v);
                if (sel_start < sel_end) {
                    float char_len = std::max(run.source_range.char_len(), std::size_t(1));
                    float ratio_start = static_cast<float>(sel_start - run.source_range.start.v) / static_cast<float>(char_len);
                    float ratio_end = static_cast<float>(sel_end - run.source_range.start.v) / static_cast<float>(char_len);
                    float x = run.origin.x + ratio_start * run.width;
                    float w = (ratio_end - ratio_start) * run.width;
                    SelectionRect sr;
                    sr.source_range = CharRange(CharOffset(sel_start), CharOffset(sel_end));
                    sr.rect = LogicalRect(x, ch.line.rect.y, std::max(w, 1.0f), ch.line.rect.height);
                    sg.ranges.push_back(std::move(sr));
                }
            }
        }
    }
    return sg;
}

struct CaretGeometry {
    CharOffset position{};
    LogicalRect rect;
    TextAffinity affinity = TextAffinity::Downstream;
};

inline std::optional<CaretGeometry> compute_caret_geometry(const LayoutTree& tree, CharOffset pos) {
    for (const auto& b : tree.blocks) {
        for (const auto& ch : b.children) if (ch.kind == LayoutItem::Kind::Line) {
            for (const auto& run : ch.line.runs) {
                if (pos.v >= run.source_range.start.v && pos.v <= run.source_range.end.v) {
                    float char_len = run.source_range.char_len();
                    float ratio = char_len ? static_cast<float>(pos.v - run.source_range.start.v) / static_cast<float>(char_len) : 0.0f;
                    float x = run.origin.x + ratio * run.width;
                    CaretGeometry cg;
                    cg.position = pos;
                    cg.rect = LogicalRect(x, ch.line.rect.y, 2.0f, ch.line.rect.height);
                    cg.affinity = TextAffinity::Downstream;
                    return cg;
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace elmd
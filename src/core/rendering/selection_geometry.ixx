// folia.core.selection_geometry — selection / caret geometry in block-local coordinates.
export module folia.core.selection_geometry;
import std;
import folia.core.types;
import folia.core.layout_tree;
import folia.core.selection;
import folia.core.text_edit;

export namespace folia {

struct SelectionRect {
    TextSpan source_span;
    LogicalRect rect;
};

struct SelectionGeometry {
    std::uint64_t revision{};
    std::vector<SelectionRect> ranges;
};

struct VisualRun {
    const GlyphRunLayout* run = nullptr;
    const TextLineLayout* line = nullptr;
};

inline std::vector<VisualRun> visual_runs(const LayoutTree& tree) {
    std::vector<VisualRun> result;
    for (const auto& block : tree.blocks) {
        for (const auto& child : block.children) if (child.kind == LayoutItem::Kind::Line) {
            for (const auto& run : child.line.runs) result.push_back({&run, &child.line});
        }
    }
    return result;
}

inline std::size_t visual_endpoint(const std::vector<VisualRun>& runs, const TextPosition& position) {
    std::optional<std::size_t> first_source;
    std::optional<std::size_t> last_source;
    std::optional<std::size_t> first_suffix;
    std::optional<std::size_t> last_prefix;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const auto& run = *runs[index].run;
        const auto& span = run.source_span;
        if (span.container_id != position.container_id || !span.source_range.covers(position.source_offset)) continue;
        if (!run.generated_boundary_affinity) {
            if (!first_source) first_source = index;
            last_source = index;
        } else if (*run.generated_boundary_affinity == TextAffinity::Downstream) {
            last_prefix = index;
        } else if (!first_suffix) {
            first_suffix = index;
        }
    }
    if (first_source) return position.affinity == TextAffinity::Upstream ? *first_source : *last_source;
    if (last_prefix) return *last_prefix;
    if (first_suffix) return *first_suffix;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (runs[index].run->source_span.container_id == position.container_id) return index;
    }
    return runs.size();
}

inline SelectionGeometry compute_selection_geometry(const LayoutTree& tree, const TextSelection& selection) {
    SelectionGeometry geometry;
    geometry.revision = tree.revision;
    if (selection.is_caret()) return geometry;
    const auto runs = visual_runs(tree);
    auto anchor_index = visual_endpoint(runs, selection.anchor);
    auto active_index = visual_endpoint(runs, selection.active);
    if (anchor_index == runs.size() || active_index == runs.size()) return geometry;
    auto start_position = selection.anchor;
    auto end_position = selection.active;
    if (active_index < anchor_index || (active_index == anchor_index && end_position.source_offset < start_position.source_offset)) {
        std::swap(anchor_index, active_index);
        std::swap(start_position, end_position);
    }
    for (std::size_t index = anchor_index; index <= active_index; ++index) {
        const auto& run = *runs[index].run;
        auto start = run.source_span.source_range.start;
        auto end = run.source_span.source_range.end;
        if (run.source_span.container_id == start_position.container_id) start = (std::max)(start, start_position.source_offset);
        if (run.source_span.container_id == end_position.container_id) end = (std::min)(end, end_position.source_offset);
        if (start >= end) continue;
        const auto length = (std::max)(run.source_span.source_range.length(), std::size_t{1});
        const auto ratio_start = static_cast<float>(start - run.source_span.source_range.start) / static_cast<float>(length);
        const auto ratio_end = static_cast<float>(end - run.source_span.source_range.start) / static_cast<float>(length);
        geometry.ranges.push_back({
            TextSpan{run.source_span.container_id, {start, end}},
            LogicalRect(run.origin.x + ratio_start * run.width, runs[index].line->rect.y,
                (std::max)((ratio_end - ratio_start) * run.width, 1.0f), runs[index].line->rect.height)});
    }
    return geometry;
}

struct CaretGeometry {
    TextPosition position{};
    LogicalRect rect;
};

inline std::optional<CaretGeometry> compute_caret_geometry(const LayoutTree& tree, TextPosition position) {
    std::optional<CaretGeometry> first_source;
    std::optional<CaretGeometry> last_source;
    std::optional<CaretGeometry> first_suffix;
    std::optional<CaretGeometry> last_prefix;
    for (const auto& block : tree.blocks) {
        for (const auto& child : block.children) if (child.kind == LayoutItem::Kind::Line) {
            for (const auto& run : child.line.runs) {
                const auto& span = run.source_span;
                if (span.container_id != position.container_id || !span.source_range.covers(position.source_offset)) continue;
                CaretGeometry candidate;
                candidate.position = position;
                if (run.generated_boundary_affinity) {
                    const auto x = *run.generated_boundary_affinity == TextAffinity::Downstream
                        ? run.origin.x + run.width
                        : run.origin.x;
                    candidate.rect = LogicalRect(x, child.line.rect.y, 2.0f, child.line.rect.height);
                    if (*run.generated_boundary_affinity == TextAffinity::Downstream) {
                        last_prefix = candidate;
                    } else if (!first_suffix) {
                        first_suffix = candidate;
                    }
                    continue;
                }
                const auto length = span.source_range.length();
                const auto ratio = length
                    ? static_cast<float>(position.source_offset - span.source_range.start) / static_cast<float>(length)
                    : 0.0f;
                candidate.rect = LogicalRect(
                    run.origin.x + ratio * run.width,
                    child.line.rect.y,
                    2.0f,
                    child.line.rect.height);
                if (!first_source) first_source = candidate;
                last_source = candidate;
            }
            if (child.line.runs.empty() && child.line.source_span.container_id == position.container_id
                && child.line.source_span.source_range.start == position.source_offset) {
                CaretGeometry candidate{
                    position,
                    LogicalRect(child.line.rect.x, child.line.rect.y, 2.0f, child.line.rect.height)};
                if (!first_source) first_source = candidate;
                last_source = candidate;
            }
        }
    }
    if (first_source) return position.affinity == TextAffinity::Upstream ? first_source : last_source;
    if (last_prefix) return last_prefix;
    if (first_suffix) return first_suffix;
    return std::nullopt;
}

} // namespace folia

// elmd.core.source_map — source ranges for AST nodes.
export module elmd.core.source_map;
import std;
import elmd.core.types;
import elmd.core.ids;

export namespace elmd {

struct NodeSourceRange {
    NodeId node_id{};
    TextRange<CharOffset> source_range;
    std::vector<TextRange<CharOffset>> marker_ranges;
    TextRange<CharOffset> content_range;

    NodeSourceRange() = default;
    NodeSourceRange(NodeId id, TextRange<CharOffset> sr, TextRange<CharOffset> cr)
        : node_id(id), source_range(sr), content_range(cr) {}
    NodeSourceRange& with_markers(std::vector<TextRange<CharOffset>> ms) {
        marker_ranges = std::move(ms); return *this;
    }
};

struct SourceMap {
    std::vector<NodeSourceRange> node_ranges;

    static SourceMap empty() { return {}; }

    const NodeSourceRange* find_node_at(CharOffset off) const {
        for (const auto& r : node_ranges) if (r.source_range.contains(off)) return &r;
        return nullptr;
    }
    const NodeSourceRange* find_node_by_id(NodeId id) const {
        for (const auto& r : node_ranges) if (r.node_id == id) return &r;
        return nullptr;
    }
};

} // namespace elmd
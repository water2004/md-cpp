// elmd.core.ids — NodeId and friends, plus IdAllocator.
export module elmd.core.ids;
import std;

export namespace elmd {

struct NodeId {
    std::uint64_t v{};
    constexpr NodeId() = default;
    constexpr explicit NodeId(std::uint64_t x) : v(x) {}
    constexpr static NodeId new_id(std::uint64_t x) { return NodeId(x); }
    constexpr bool operator==(const NodeId& o) const { return v == o.v; }
    constexpr bool operator!=(const NodeId& o) const { return v != o.v; }
    constexpr bool operator<(const NodeId& o) const { return v < o.v; }
};

struct BlockId { std::uint64_t v{}; };
struct InlineId { std::uint64_t v{}; };
struct ImageId { std::uint64_t v{}; };
struct BrushId { std::uint64_t v{}; };

// IdAllocator — allocators start at 1 (matches shared::ids in Rust).
struct IdAllocator {
    std::uint64_t next_node = 1;
    std::uint64_t next_block = 1;
    std::uint64_t next_inline = 1;
    std::uint64_t next_image = 1;
    std::uint64_t next_brush = 1;

    NodeId next_node_id() { return NodeId(next_node++); }
    BlockId next_block_id() { return BlockId{next_block++}; }
    InlineId next_inline_id() { return InlineId{next_inline++}; }
    ImageId next_image_id() { return ImageId{next_image++}; }
    BrushId next_brush_id() { return BrushId{next_brush++}; }
};

} // namespace elmd
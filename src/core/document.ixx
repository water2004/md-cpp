// elmd.core.document — EditorDocument, the parsed and editable document root.
export module elmd.core.document;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.ast;
import elmd.core.source_map;
import elmd.core.metadata;
import elmd.core.diagnostics;

export namespace elmd {

struct EditorDocument {
    std::uint64_t revision = 1;
    std::vector<BlockNode> blocks;
    SourceMap source_map;
    DocumentMetadata metadata;
    std::vector<Diagnostic> diagnostics;
    bool trailing_newline = false;

    static EditorDocument empty(std::uint64_t rev) {
        EditorDocument d;
        d.revision = rev;
        BlockNode paragraph;
        paragraph.id = NodeId(1);
        paragraph.kind = BlockKind::Paragraph;
        d.blocks.push_back(paragraph);
        d.source_map.node_ranges.emplace_back(
            paragraph.id,
            CharRange(CharOffset(0), CharOffset(0)),
            CharRange(CharOffset(0), CharOffset(0)));
        return d;
    }
};

} // namespace elmd

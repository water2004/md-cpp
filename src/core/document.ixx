// elmd.core.document — EditorDocument, the parsed and editable document root.
export module elmd.core.document;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.ast;
import elmd.core.source_map;
import elmd.core.metadata;
import elmd.core.diagnostics;
import elmd.core.dialect;

export namespace elmd {

struct EditorDocument {
    std::uint64_t revision = 1;
    MarkdownDialect dialect = default_dialect();
    BlockNode root = [] {
        BlockNode node;
        node.id = NodeId(1);
        node.kind = BlockKind::Document;
        return node;
    }();
    SourceMap source_map;
    DocumentMetadata metadata;
    std::vector<Diagnostic> diagnostics;
    bool trailing_newline = false;

    static EditorDocument empty(std::uint64_t rev) {
        EditorDocument d;
        d.revision = rev;
        BlockNode paragraph;
        paragraph.id = NodeId(2);
        paragraph.kind = BlockKind::Paragraph;
        d.root.children.push_back(paragraph);
        d.source_map.node_ranges.emplace_back(
            paragraph.id,
            CharRange(CharOffset(0), CharOffset(0)),
            CharRange(CharOffset(0), CharOffset(0)));
        return d;
    }
};

} // namespace elmd

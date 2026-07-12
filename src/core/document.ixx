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
        EditorDocument d; d.revision = rev; return d;
    }
};

} // namespace elmd

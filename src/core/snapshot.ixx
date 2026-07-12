// elmd.core.snapshot — immutable editor pipeline snapshot.
// Pure core model: no WinUI / Windows / DirectWrite dependencies.
export module elmd.core.snapshot;
import std;
import elmd.core.selection;
import elmd.core.document;
import elmd.core.render_model;
import elmd.core.layout_tree;
import elmd.core.outline;
import elmd.core.symbols;
import elmd.core.diagnostics;
import elmd.core.text_edit;

export namespace elmd {

struct EditorSnapshot {
    std::uint64_t revision = 0;
    std::string text;
    TextSelection selection;
    EditorDocument markdown_doc;
    RenderModel render_model;
    LayoutTree layout_tree;
    Outline outline;
    DocumentSymbolIndex symbols;
    std::vector<Diagnostic> diagnostics;
};

} // namespace elmd

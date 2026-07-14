// elmd.core.symbols — DocumentSymbolIndex.
export module elmd.core.symbols;
import std;
import elmd.core.ids;
import elmd.core.text_edit;

export namespace elmd {

struct HeadingSymbol   { NodeId node_id; std::uint8_t level; std::string title; std::string slug; };
struct FootnoteSymbol  { NodeId node_id; std::string label; };
struct FootnoteReferenceSymbol {
    NodeId node_id;
    NodeId container_id;
    SourceRange source_range;
    std::string label;
};
struct LinkSymbol      { NodeId node_id; std::string href; std::string text; };
struct ImageSymbol      { NodeId node_id; std::string src; std::string alt; };
struct MathSymbol       { NodeId node_id; std::string tex_preview; };
struct CodeBlockSymbol  { NodeId node_id; std::optional<std::string> language; std::size_t line_count; };

struct DocumentSymbolIndex {
    std::vector<HeadingSymbol> headings;
    std::vector<FootnoteSymbol> footnotes;
    std::vector<FootnoteReferenceSymbol> footnote_references;
    std::vector<LinkSymbol>     links;
    std::vector<ImageSymbol>    images;
    std::vector<MathSymbol>     math_blocks;
    std::vector<CodeBlockSymbol> code_blocks;
};

} // namespace elmd

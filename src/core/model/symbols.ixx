// folia.core.symbols — DocumentSymbolIndex.
export module folia.core.symbols;
import std;
import folia.core.ids;
import folia.core.text_edit;

export namespace folia {

struct HeadingSymbol   {
    NodeId node_id;
    std::uint8_t level;
    std::string title;
    std::string slug;
    bool operator==(const HeadingSymbol&) const = default;
};
struct FootnoteSymbol  {
    NodeId node_id;
    std::string label;
    bool operator==(const FootnoteSymbol&) const = default;
};
struct FootnoteReferenceSymbol {
    NodeId node_id;
    NodeId container_id;
    SourceRange source_range;
    std::string label;
    bool operator==(const FootnoteReferenceSymbol&) const = default;
};
struct LinkSymbol {
    NodeId node_id;
    std::string href;
    std::string text;
    bool operator==(const LinkSymbol&) const = default;
};
struct ImageSymbol {
    NodeId node_id;
    std::string src;
    std::string alt;
    bool operator==(const ImageSymbol&) const = default;
};
struct MathSymbol {
    NodeId node_id;
    std::string tex_preview;
    bool operator==(const MathSymbol&) const = default;
};
struct CodeBlockSymbol {
    NodeId node_id;
    std::optional<std::string> language;
    std::size_t line_count;
    bool operator==(const CodeBlockSymbol&) const = default;
};

struct DocumentSymbolIndex {
    std::vector<HeadingSymbol> headings;
    std::vector<FootnoteSymbol> footnotes;
    std::vector<FootnoteReferenceSymbol> footnote_references;
    std::vector<LinkSymbol>     links;
    std::vector<ImageSymbol>    images;
    std::vector<MathSymbol>     math_blocks;
    std::vector<CodeBlockSymbol> code_blocks;

    bool operator==(const DocumentSymbolIndex&) const = default;
};

} // namespace folia

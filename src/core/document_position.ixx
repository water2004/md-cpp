export module elmd.core.document_position;
import std;
import elmd.core.ids;
import elmd.core.selection;

export namespace elmd {

enum class DocumentPositionPart {
    Content,
    OpeningMarker,
    ClosingMarker,
};

struct DocumentPosition {
    NodeId node_id{};
    std::size_t offset = 0;
    TextAffinity affinity = TextAffinity::Downstream;
    NodeId inline_node_id{};
    DocumentPositionPart part = DocumentPositionPart::Content;
    std::size_t part_offset = 0;

    bool operator==(const DocumentPosition&) const = default;
};

struct DocumentSelection {
    DocumentPosition anchor;
    DocumentPosition active;

    static DocumentSelection caret(DocumentPosition position) {
        return DocumentSelection{position, position};
    }

    bool is_caret() const { return anchor == active; }
};

}

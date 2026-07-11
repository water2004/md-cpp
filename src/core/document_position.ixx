export module elmd.core.document_position;
import std;
import elmd.core.ids;
import elmd.core.selection;

export namespace elmd {

struct DocumentPosition {
    NodeId node_id{};
    std::size_t offset = 0;
    TextAffinity affinity = TextAffinity::Downstream;

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

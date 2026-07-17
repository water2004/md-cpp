// elmd.core.ast — unified Markdown block tree plus block-local inline CST.
// CommonMark+GFM+math+toc+frontmatter+footnotes+definition_lists+callouts+
// wiki_links+tables+images and safe recursive HTML semantics.
export module elmd.core.ast;
import std;
import elmd.core.block_source;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.image_dimension;
import elmd.core.text_edit;
import elmd.core.inline_document;
import elmd.core.html_cst;

export namespace elmd {

struct BlockNode;

using BlockVec = std::vector<BlockNode>;

enum class BlockKind {
    Document,
    Paragraph, Heading, CalloutTitle, BlockQuote, List, TaskList, ListItem, TaskListItem,
    CodeBlock, MathBlock, Table, TableRow, TableCell,
    ImageBlock, Callout, FootnoteDefinition, Toc, Frontmatter,
    ThematicBreak, LinkDefinition, UnsupportedMarkup, HtmlContainer, Extension,
};

inline bool is_editable_block_owner(BlockKind kind) {
    switch (kind) {
        case BlockKind::Paragraph:
        case BlockKind::Heading:
        case BlockKind::CalloutTitle:
        case BlockKind::TableCell:
        case BlockKind::CodeBlock:
        case BlockKind::MathBlock:
        case BlockKind::ImageBlock:
        case BlockKind::Toc:
        case BlockKind::Frontmatter:
        case BlockKind::ThematicBreak:
        case BlockKind::LinkDefinition:
        case BlockKind::UnsupportedMarkup:
        case BlockKind::Extension:
            return true;
        default:
            return false;
    }
}

struct BlockTextSpecial {
    std::uint8_t level = 0;             // Heading
    std::string slug;                   // Heading
    std::u32string opening_marker;
    std::u32string closing_marker;
};

struct BlockListSpecial {
    bool ordered = false;
    std::uint64_t start = 1;
    char32_t delimiter = U'.';
};

struct BlockItemSpecial {
    std::u32string marker;              // ListItem / TaskListItem
    bool checked = false;               // TaskListItem
};

struct BlockAtomicSpecial {
    bool code_indented = false;
    MathDelimiter math_delim = MathDelimiter::BlockDollar; // MathBlock / InlineMath (in inline)
    TocMarkerKind toc_marker = TocMarkerKind::BracketToc; // Toc
    FrontmatterFormat fmt = FrontmatterFormat::Yaml; // Frontmatter
    std::string raw;                    // Frontmatter.raw / UnsupportedMarkup.raw
    UnsupportedMarkupReason unsup_reason = UnsupportedMarkupReason::UnsafeHtml;
    std::string ext_name;               // Extension
};

struct BlockTableSpecial {
    std::vector<TableAlignment> table_aligns;
    // Exact block syntax that is not owned by TableCell inline sources.
    // One internal line ending joins each logical table line to the next;
    // the final row's ending remains ordinary sibling separator metadata.
    std::u32string table_separator_source;
    std::vector<std::u32string> table_internal_line_endings;
    bool table_header_row = false;      // TableRow
};

struct BlockImageSpecial {
    std::string src;        // ImageBlock
    std::string image_alt;
    std::optional<std::string> image_title;
    std::optional<std::string> image_link;
    std::optional<ImageDimension> image_width;
    std::optional<ImageDimension> image_height;
};

struct BlockContainerSpecial {
    std::string callout_kind;           // Callout
    std::string footnote_label;        // FootnoteDefinition
};

struct HtmlContentSlot {
    NodeId owner_id{};
    SourceRange source_range;
};

struct HtmlBlockShapeEntry {
    BlockKind kind = BlockKind::Paragraph;
    std::size_t depth = 0;
    std::size_t child_count = 0;

    auto operator<=>(const HtmlBlockShapeEntry&) const = default;
};

struct BlockHtmlSpecial {
    std::u32string source;
    HtmlCstTree tree;
    std::vector<HtmlContentSlot> content_slots;
    std::vector<HtmlBlockShapeEntry> structure_shape;
    std::string root_tag;
    std::u32string opening_marker;
    std::u32string closing_marker;
};

// Rare, independently optional payloads share one indirection so the common
// BlockNodeSpecial stays compact.  These values may coexist (an HTML-backed
// table can also carry container or image metadata), so this is deliberately
// not a variant.
struct BlockSupplementalSpecial {
    std::unique_ptr<BlockImageSpecial> image;
    std::unique_ptr<BlockContainerSpecial> container;
    std::unique_ptr<BlockHtmlSpecial> html;

    BlockSupplementalSpecial() = default;
    BlockSupplementalSpecial(BlockSupplementalSpecial const& other)
        : image(other.image ? std::make_unique<BlockImageSpecial>(*other.image) : nullptr),
          container(other.container ? std::make_unique<BlockContainerSpecial>(*other.container) : nullptr),
          html(other.html ? std::make_unique<BlockHtmlSpecial>(*other.html) : nullptr) {}
    BlockSupplementalSpecial& operator=(BlockSupplementalSpecial const& other) {
        if (this == &other) return *this;
        *this = BlockSupplementalSpecial(other);
        return *this;
    }
    BlockSupplementalSpecial(BlockSupplementalSpecial&&) noexcept = default;
    BlockSupplementalSpecial& operator=(BlockSupplementalSpecial&&) noexcept = default;
};

struct BlockNodeSpecial {
    std::unique_ptr<BlockTextSpecial> text;
    std::unique_ptr<BlockListSpecial> list;
    std::unique_ptr<BlockItemSpecial> item;
    std::unique_ptr<BlockAtomicSpecial> atomic;
    std::unique_ptr<BlockTableSpecial> table;
    std::unique_ptr<BlockSupplementalSpecial> supplemental;

    BlockNodeSpecial() = default;
    BlockNodeSpecial(BlockNodeSpecial const& other)
        : text(other.text ? std::make_unique<BlockTextSpecial>(*other.text) : nullptr),
          list(other.list ? std::make_unique<BlockListSpecial>(*other.list) : nullptr),
          item(other.item ? std::make_unique<BlockItemSpecial>(*other.item) : nullptr),
          atomic(other.atomic ? std::make_unique<BlockAtomicSpecial>(*other.atomic) : nullptr),
          table(other.table ? std::make_unique<BlockTableSpecial>(*other.table) : nullptr),
          supplemental(other.supplemental
              ? std::make_unique<BlockSupplementalSpecial>(*other.supplemental)
              : nullptr) {}
    BlockNodeSpecial& operator=(BlockNodeSpecial const& other) {
        if (this == &other) return *this;
        *this = BlockNodeSpecial(other);
        return *this;
    }
    BlockNodeSpecial(BlockNodeSpecial&&) noexcept = default;
    BlockNodeSpecial& operator=(BlockNodeSpecial&&) noexcept = default;
};

struct BlockNode {
    NodeId id{};
    BlockKind kind = BlockKind::Paragraph;
    BlockVec children;                  // the one structural child collection for every container
    InlineDocument inline_content;      // Paragraph / Heading / CalloutTitle / TableCell
    BlockSourceDocument block_source;   // CodeBlock / MathBlock, exact full Markdown source
    // Exact source between the preceding direct sibling and this block.
    // Separators are common on ordinary paragraphs, so keeping this compact
    // optional beside the node avoids allocating the much larger kind payload
    // merely to retain a newline.  It is serializer metadata, never an editing
    // coordinate or part of the block-local source.
    std::optional<std::u32string> separator_before;
    // Paragraphs and structural containers must not carry the full payload of
    // every unrelated block kind. The optional payload retains ordinary value
    // semantics through the deep-copy operations below.
    std::unique_ptr<BlockNodeSpecial> payload;

    BlockNode() = default;
    BlockNode(BlockNode const& other)
        : id(other.id), kind(other.kind), children(other.children),
          inline_content(other.inline_content), block_source(other.block_source),
          separator_before(other.separator_before),
          payload(other.payload ? std::make_unique<BlockNodeSpecial>(*other.payload) : nullptr) {}
    BlockNode& operator=(BlockNode const& other) {
        if (this == &other) return *this;
        id = other.id;
        kind = other.kind;
        children = other.children;
        inline_content = other.inline_content;
        block_source = other.block_source;
        separator_before = other.separator_before;
        payload = other.payload ? std::make_unique<BlockNodeSpecial>(*other.payload) : nullptr;
        return *this;
    }
    BlockNode(BlockNode&&) noexcept = default;
    BlockNode& operator=(BlockNode&&) noexcept = default;

    BlockNodeSpecial const& special() const {
        static const BlockNodeSpecial empty{};
        return payload ? *payload : empty;
    }

    BlockNodeSpecial& ensure_special() {
        if (!payload) payload = std::make_unique<BlockNodeSpecial>();
        return *payload;
    }

    BlockTextSpecial const& text_special() const {
        static const BlockTextSpecial empty{};
        return payload && payload->text ? *payload->text : empty;
    }
    BlockTextSpecial& ensure_text_special() {
        auto& special = ensure_special();
        if (!special.text) special.text = std::make_unique<BlockTextSpecial>();
        return *special.text;
    }
    BlockListSpecial const& list_special() const {
        static const BlockListSpecial empty{};
        return payload && payload->list ? *payload->list : empty;
    }
    BlockListSpecial& ensure_list_special() {
        auto& special = ensure_special();
        if (!special.list) special.list = std::make_unique<BlockListSpecial>();
        return *special.list;
    }
    BlockItemSpecial const& item_special() const {
        static const BlockItemSpecial empty{};
        return payload && payload->item ? *payload->item : empty;
    }
    BlockItemSpecial& ensure_item_special() {
        auto& special = ensure_special();
        if (!special.item) special.item = std::make_unique<BlockItemSpecial>();
        return *special.item;
    }
    BlockAtomicSpecial const& atomic_special() const {
        static const BlockAtomicSpecial empty{};
        return payload && payload->atomic ? *payload->atomic : empty;
    }
    BlockAtomicSpecial& ensure_atomic_special() {
        auto& special = ensure_special();
        if (!special.atomic) special.atomic = std::make_unique<BlockAtomicSpecial>();
        return *special.atomic;
    }
    BlockTableSpecial const& table_special() const {
        static const BlockTableSpecial empty{};
        return payload && payload->table ? *payload->table : empty;
    }
    BlockTableSpecial& ensure_table_special() {
        auto& special = ensure_special();
        if (!special.table) special.table = std::make_unique<BlockTableSpecial>();
        return *special.table;
    }
    BlockImageSpecial const& image_special() const {
        static const BlockImageSpecial empty{};
        return payload && payload->supplemental && payload->supplemental->image
            ? *payload->supplemental->image
            : empty;
    }
    BlockImageSpecial& ensure_image_special() {
        auto& special = ensure_special();
        if (!special.supplemental) {
            special.supplemental = std::make_unique<BlockSupplementalSpecial>();
        }
        if (!special.supplemental->image) {
            special.supplemental->image = std::make_unique<BlockImageSpecial>();
        }
        return *special.supplemental->image;
    }
    BlockContainerSpecial const& container_special() const {
        static const BlockContainerSpecial empty{};
        return payload && payload->supplemental && payload->supplemental->container
            ? *payload->supplemental->container
            : empty;
    }
    BlockContainerSpecial& ensure_container_special() {
        auto& special = ensure_special();
        if (!special.supplemental) {
            special.supplemental = std::make_unique<BlockSupplementalSpecial>();
        }
        if (!special.supplemental->container) {
            special.supplemental->container = std::make_unique<BlockContainerSpecial>();
        }
        return *special.supplemental->container;
    }

    BlockHtmlSpecial const& html_special() const {
        static const BlockHtmlSpecial empty{};
        return payload && payload->supplemental && payload->supplemental->html
            ? *payload->supplemental->html
            : empty;
    }
    BlockHtmlSpecial& ensure_html_special() {
        auto& special = ensure_special();
        if (!special.supplemental) {
            special.supplemental = std::make_unique<BlockSupplementalSpecial>();
        }
        if (!special.supplemental->html) {
            special.supplemental->html = std::make_unique<BlockHtmlSpecial>();
        }
        return *special.supplemental->html;
    }

    bool has_html_source() const {
        return payload && payload->supplemental && payload->supplemental->html
            && !payload->supplemental->html->source.empty();
    }

    bool has_html_element_provenance() const {
        return payload && payload->supplemental && payload->supplemental->html
            && !payload->supplemental->html->root_tag.empty();
    }
};

inline std::vector<HtmlBlockShapeEntry> html_block_structure_shape(const BlockNode& root) {
    std::vector<HtmlBlockShapeEntry> shape;
    auto collect = [&](auto& self, const BlockNode& block, std::size_t depth) -> void {
        shape.push_back({block.kind, depth, block.children.size()});
        for (const auto& child : block.children) self(self, child, depth + 1);
    };
    collect(collect, root, 0);
    return shape;
}

// Heading sibling helper used in several places.
inline const InlineDocument& heading_inline_content(const BlockNode& b) { return b.inline_content; }
inline bool is_heading_block(const BlockNode& b) { return b.kind == BlockKind::Heading; }

} // namespace elmd

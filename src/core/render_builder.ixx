// elmd.core.render_builder — build_render_model(doc, source_text, outline).
// Walks the AST and produces a RenderModel, extracting markers (`**`/`*`/`~~`
// etc.) with the source-range accumulation method. Faithful port of the Rust
// builder — the layout later re-derives visibility from the live caret.
export module elmd.core.render_builder;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.ast;
import elmd.core.source_map;
import elmd.core.document;
import elmd.core.outline;
import elmd.core.diagnostics;
import elmd.core.utf;
import elmd.core.render_model;

export namespace elmd {

inline RenderDiagnostic convert_diagnostic(const Diagnostic& d) {
    RenderDiagnostic r;
    r.severity = (d.severity == DiagnosticSeverity::Hint)   ? RenderDiagnostic::Sev::Info
               : (d.severity == DiagnosticSeverity::Error)   ? RenderDiagnostic::Sev::Error
               : RenderDiagnostic::Sev::Warning;
    r.message = d.message; r.source_range = d.source_range;
    return r;
}

inline RenderBlock render_block_base(BlockKind k, NodeId id, TextRange<CharOffset> sr, BlockStyle bs) {
    RenderBlock b; b.kind = [&](BlockKind)->RenderBlockKind {
        switch (k) {
            case BlockKind::Paragraph: case BlockKind::Heading:
            case BlockKind::BlockQuote: case BlockKind::List: case BlockKind::TaskList:
                return RenderBlockKind::Text;
            case BlockKind::CodeBlock: return RenderBlockKind::Code;
            case BlockKind::MathBlock: return RenderBlockKind::Math;
            case BlockKind::Table:     return RenderBlockKind::Table;
            case BlockKind::ImageBlock:return RenderBlockKind::Image;
            case BlockKind::Toc:       return RenderBlockKind::Toc;
            case BlockKind::Callout:   return RenderBlockKind::Callout;
            case BlockKind::FootnoteDefinition: return RenderBlockKind::Footnote;
            case BlockKind::Frontmatter:return RenderBlockKind::Frontmatter;
            case BlockKind::ThematicBreak: return RenderBlockKind::Text;
            case BlockKind::UnsupportedMarkup: return RenderBlockKind::Unsupported;
            case BlockKind::Extension: return RenderBlockKind::Extension;
        }
        return RenderBlockKind::Text;
    }(k);
    b.id = id; b.source_range = sr; b.block_style = bs;
    b.content_range = sr;
    return b;
}

// Build a Marker inline item at the running cursor; advances `cursor`.
inline void push_marker(std::vector<InlineRenderItem>& out, std::size_t& cursor, std::u32string text) {
    InlineRenderItem m; m.kind = InlineRenderItem::Kind::Marker;
    m.source_range = CharRange(CharOffset(cursor), CharOffset(cursor + text.size()));
    m.text = std::move(text); m.marker_style = MarkerStyle{true, {}}; m.visibility = MarkerVisibility::Always;
    out.push_back(std::move(m));
    cursor += out.back().text.size();
}

struct Builder {
    const SourceMap* sm = nullptr;
    std::vector<InlineRenderItem> build_inlines(const InlineVec& nodes, std::size_t content_start, const InlineStyle& base) {
        std::vector<InlineRenderItem> out;
        std::size_t cursor = content_start;
        for (const auto& node : nodes) build_inline(node, out, cursor, base);
        return out;
    }
    void build_inline(const InlineNode& node, std::vector<InlineRenderItem>& out, std::size_t& cursor, const InlineStyle& style) {
        using K = InlineKind;
        auto range_for = [&](NodeId id, std::size_t fallback_end) -> TextRange<CharOffset> {
            if (const auto* r = sm ? sm->find_node_by_id(id) : nullptr) return r->source_range;
            return CharRange(CharOffset(cursor), CharOffset(fallback_end));
        };
        auto content_start_for = [&](NodeId id) -> std::size_t {
            if (const auto* r = sm ? sm->find_node_by_id(id) : nullptr) return r->content_range.start.v;
            return cursor;
        };
        switch (node.kind) {
            case K::Text: {
                std::size_t len = node.text.size();
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it = InlineRenderItem::plain_text(node.text, sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::Strong: {
                push_marker(out, cursor, U"**");
                std::size_t inner = content_start_for(node.id);
                InlineStyle cs = style; cs.bold = true;
                auto child_items = build_inlines(node.children, inner, cs);
                for (auto& it : child_items) { cursor += it.source_range.len(); out.push_back(std::move(it)); }
                push_marker(out, cursor, U"**");
                return;
            }
            case K::Emphasis: {
                push_marker(out, cursor, U"*");
                std::size_t inner = content_start_for(node.id);
                InlineStyle cs = style; cs.italic = true;
                auto child_items = build_inlines(node.children, inner, cs);
                for (auto& it : child_items) { cursor += it.source_range.len(); out.push_back(std::move(it)); }
                push_marker(out, cursor, U"*");
                return;
            }
            case K::Strike: {
                push_marker(out, cursor, U"~~");
                std::size_t inner = content_start_for(node.id);
                InlineStyle cs = style; cs.strikethrough = true;
                auto child_items = build_inlines(node.children, inner, cs);
                for (auto& it : child_items) { cursor += it.source_range.len(); out.push_back(std::move(it)); }
                push_marker(out, cursor, U"~~");
                return;
            }
            case K::InlineCode: {
                auto sr = range_for(node.id, cursor + node.text.size() + 2);
                (void)sr;
                push_marker(out, cursor, U"`");
                InlineStyle cs = style; cs.code = true;
                InlineRenderItem it = InlineRenderItem::plain_text(node.text, CharRange(CharOffset(cursor), CharOffset(cursor + node.text.size())));
                it.style = cs;
                out.push_back(std::move(it));
                cursor += node.text.size();
                push_marker(out, cursor, U"`");
                return;
            }
            case K::InlineMath: {
                std::size_t len = node.text.size() + 2;
                auto sr = range_for(node.id, cursor + len);
                MathDisplayMode disp = (node.math_delim == MathDelimiter::BlockDollar
                                        || node.math_delim == MathDelimiter::BlockBracket
                                        || node.math_delim == MathDelimiter::FencedMath)
                                       ? MathDisplayMode::Block : MathDisplayMode::Inline;
                InlineRenderItem it; it.kind = InlineRenderItem::Kind::Math;
                it.id = node.id; it.source_range = sr; it.text = node.text; it.display = disp;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::Link: {
                std::size_t inner = content_start_for(node.id);
                InlineStyle ls = style; ls.link = true;
                auto child_items = build_inlines(node.children, inner, ls);
                std::size_t consumed = 0;
                if (const auto* r = sm ? sm->find_node_by_id(node.id) : nullptr) consumed = r->source_range.len();
                else {
                    // fallback: sum grandchildren text + 4
                    std::size_t sum = 0;
                    for (const auto& c : node.children) sum += inline_text_content(c).size();
                    consumed = sum + 4;
                }
                InlineRenderItem it; it.kind = InlineRenderItem::Kind::Link;
                it.id = node.id; it.source_range = range_for(node.id, cursor + consumed);
                it.href = node.href; it.children = std::move(child_items);
                out.push_back(std::move(it));
                cursor += consumed;
                return;
            }
            case K::Image: {
                std::size_t len = node.alt.size() + 5;
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it; it.kind = InlineRenderItem::Kind::Image;
                it.id = node.id; it.source_range = sr; it.src = node.href; it.alt = node.alt;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::FootnoteRef: {
                std::u32string raw = U"[^" + node.label.empty() ? U"" : utf8_to_cps(node.label);
                raw.push_back(']');
                std::size_t len = raw.size();
                auto sr = range_for(node.id, cursor + len);
                InlineStyle ls = style; ls.link = true;
                InlineRenderItem it = InlineRenderItem::plain_text(raw, sr);
                it.style = ls;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::WikiLink: {
                std::u32string raw;
                raw = U"[[" + utf8_to_cps(node.target);
                if (node.alias) { raw.push_back('|'); raw += utf8_to_cps(*node.alias); }
                raw += U"]]";
                std::size_t len = raw.size();
                auto sr = range_for(node.id, cursor + len);
                InlineStyle ls = style; ls.link = true;
                InlineRenderItem it = InlineRenderItem::plain_text(raw, sr);
                it.style = ls;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::SoftBreak: {
                auto sr = range_for(node.id, cursor + 1);
                InlineRenderItem it = InlineRenderItem::plain_text(U"\n", sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor += 1;
                return;
            }
            case K::HardBreak: {
                auto sr = range_for(node.id, cursor + 3);
                InlineRenderItem it = InlineRenderItem::plain_text(U"  \n", sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor += 3;
                return;
            }
            case K::UnsupportedMarkup: {
                std::size_t len = node.text.size();
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it = InlineRenderItem::plain_text(node.text, sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
            case K::Extension: {
                std::u32string raw = node.ext_text.empty()
                    ? U"[ext:" + utf8_to_cps(node.ext_name) + U"]"
                    : U"[ext:" + utf8_to_cps(node.ext_name) + U":" + node.ext_text + U"]";
                std::size_t len = raw.size();
                auto sr = range_for(node.id, cursor + len);
                InlineRenderItem it = InlineRenderItem::plain_text(raw, sr);
                it.style = style;
                out.push_back(std::move(it));
                cursor += len;
                return;
            }
        }
    }

    RenderBlock build_block(const BlockNode& b) {
        using BK = BlockKind;
        auto base_range = [&]() -> TextRange<CharOffset> {
            if (const auto* r = sm ? sm->find_node_by_id(b.id) : nullptr) return r->source_range;
            return CharRange(CharOffset(0), CharOffset(0));
        };
        auto content_range = [&]() -> TextRange<CharOffset> {
            if (const auto* r = sm ? sm->find_node_by_id(b.id) : nullptr) return r->content_range;
            return base_range();
        };
        auto with_content_range = [&](RenderBlock rb) {
            rb.content_range = content_range();
            return rb;
        };
        switch (b.kind) {
            case BK::Paragraph: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
                InlineStyle s = InlineStyle::plain();
                rb.inline_items = build_inlines(b.children, base_range().start.v, s);
                return with_content_range(std::move(rb));
            }
            case BK::Heading: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::heading(b.level));
                InlineStyle s = InlineStyle::plain(); s.heading_level = b.level;
                // content_start: skip "# "
                std::size_t cs = base_range().start.v;
                if (const auto* r = sm ? sm->find_node_by_id(b.id) : nullptr) cs = r->content_range.start.v;
                rb.inline_items = build_inlines(b.children, cs, s);
                return with_content_range(std::move(rb));
            }
            case BK::BlockQuote: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::blockquote());
                std::size_t cursor = base_range().start.v;
                for (const auto& ch : b.quote_children) {
                    if (ch.kind == BlockKind::Paragraph) {
                        auto items = build_inlines(ch.children, cursor, InlineStyle::plain());
                        for (auto& it : items) { cursor += it.source_range.len(); rb.inline_items.push_back(std::move(it)); }
                        push_marker(rb.inline_items, cursor, U"\n");
                    }
                }
                return with_content_range(std::move(rb));
            }
            case BK::List: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
                std::size_t cursor = base_range().start.v;
                for (std::size_t i = 0; i < b.list_items.size(); ++i) {
                    std::u32string m = (i == 0 ? U"- " : U"- "); // ordered lists get "N. " in v2
                    push_marker(rb.inline_items, cursor, m);
                    for (const auto& ch : b.list_items[i].children) {
                        if (ch.kind == BlockKind::Paragraph) {
                            auto items = build_inlines(ch.children, cursor, InlineStyle::plain());
                            for (auto& it : items) { cursor += it.source_range.len(); rb.inline_items.push_back(std::move(it)); }
                        }
                    }
                }
                return with_content_range(std::move(rb));
            }
            case BK::TaskList: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
                std::size_t cursor = base_range().start.v;
                for (const auto& ti : b.task_items) {
                    std::u32string m = ti.checked ? U"[x] " : U"[ ] ";
                    push_marker(rb.inline_items, cursor, m);
                    for (const auto& ch : ti.children) {
                        if (ch.kind == BlockKind::Paragraph) {
                            auto items = build_inlines(ch.children, cursor, InlineStyle::plain());
                            for (auto& it : items) { cursor += it.source_range.len(); rb.inline_items.push_back(std::move(it)); }
                        }
                    }
                }
                return with_content_range(std::move(rb));
            }
            case BK::CodeBlock: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::code());
                rb.language = b.language;
                rb.code_text = b.code_text;
                std::size_t n = 1; for (char32_t c : b.code_text) if (c == '\n') ++n;
                rb.line_count = n;
                return with_content_range(std::move(rb));
            }
            case BK::MathBlock: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::math());
                rb.tex = b.tex; rb.math_delim = b.math_delim;
                return with_content_range(std::move(rb));
            }
            case BK::Table: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::table());
                rb.table_aligns = b.table_aligns;
                rb.column_count = std::max(b.table_header.size(), b.table_rows.empty() ? 0 : b.table_rows[0].cells.size());
                rb.row_count = 1 + b.table_rows.size();
                // header cells
                for (const auto& c : b.table_header) {
                    auto items = build_inlines(c.children, base_range().start.v, InlineStyle::plain());
                    rb.table_cells.push_back(std::move(items));
                }
                for (const auto& row : b.table_rows) {
                    for (const auto& c : row.cells) {
                        auto items = build_inlines(c.children, base_range().start.v, InlineStyle::plain());
                        rb.table_cells.push_back(std::move(items));
                    }
                }
                return with_content_range(std::move(rb));
            }
            case BK::ImageBlock: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::image());
                rb.alt = b.image_alt; rb.src = b.src;
                return with_content_range(std::move(rb));
            }
            case BK::Callout: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::callout(b.callout_kind));
                rb.callout_kind = b.callout_kind;
                if (b.callout_title) {
                    std::vector<InlineRenderItem> items;
                    items = build_inlines(*b.callout_title, base_range().start.v, InlineStyle::plain());
                    rb.callout_title = std::move(items);
                }
                for (const auto& ch : b.quote_children) rb.child_blocks.push_back(build_block(ch));
                return with_content_range(std::move(rb));
            }
            case BK::FootnoteDefinition: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::footnote());
                rb.footnote_label = b.footnote_label;
                for (const auto& ch : b.quote_children) rb.child_blocks.push_back(build_block(ch));
                return with_content_range(std::move(rb));
            }
            case BK::Toc: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::toc());
                return with_content_range(std::move(rb));
            }
            case BK::Frontmatter: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::frontmatter());
                rb.raw = b.raw;
                return with_content_range(std::move(rb));
            }
            case BK::ThematicBreak: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
                InlineRenderItem m; m.kind = InlineRenderItem::Kind::Marker;
                m.source_range = CharRange(CharOffset(0), CharOffset(9));
                m.text = U"─────────";
                m.marker_style = MarkerStyle{true, {}}; m.visibility = MarkerVisibility::Always;
                rb.inline_items.push_back(std::move(m));
                return with_content_range(std::move(rb));
            }
            case BK::UnsupportedMarkup: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::unsupported());
                rb.raw = b.raw;
                rb.reason_text = unsupported_reason_message(b.unsup_reason);
                return with_content_range(std::move(rb));
            }
            case BK::Extension: {
                auto rb = render_block_base(b.kind, b.id, base_range(), BlockStyle::extension());
                rb.extension_name = b.ext_name;
                return with_content_range(std::move(rb));
            }
        }
        return render_block_base(b.kind, b.id, base_range(), BlockStyle::paragraph());
    }
};

inline RenderModel build_render_model(const MarkdownDocument& doc,
                                      const std::string& /*source_text*/,
                                      const Outline& outline) {
    Builder bd; bd.sm = &doc.source_map;
    std::vector<RenderBlock> blocks;
    for (const auto& b : doc.blocks) blocks.push_back(bd.build_block(b));
    std::vector<RenderDiagnostic> diags;
    for (const auto& d : doc.diagnostics) diags.push_back(convert_diagnostic(d));
    RenderModel m; m.revision = doc.revision; m.blocks = std::move(blocks);
    m.outline = outline; m.diagnostics = std::move(diags);
    return m;
}

} // namespace elmd

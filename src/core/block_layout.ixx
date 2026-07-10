// elmd.core.block_layout — layout_blocks + helpers. Bakes viewport_origin into
// every rect/origin (single screen-coordinate system, HANDOFF invariant #1).
export module elmd.core.block_layout;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.render_model;
import elmd.core.text_measurer;
import elmd.core.utf;
import elmd.core.layout_tree;
import elmd.core.outline;

export namespace elmd {

inline MarkerVisibility marker_visibility_for(TextRange<CharOffset> range, std::optional<CharOffset> caret) {
    if (caret && range.contains(*caret)) return MarkerVisibility::Always;
    return MarkerVisibility::HiddenButEditable;
}

inline std::pair<LayoutBlock, float> layout_text_block(const RenderBlock& rb, float y,
                                                       float viewport_width, float scale,
                                                       TextMeasurer& measurer,
                                                       std::optional<CharOffset> caret,
                                                       LogicalPoint origin) {
    const auto& style = rb.block_style;
    const char32_t strip_chars[] = {' ', '\t'};
    (void)strip_chars;
    float font_size = 16.0f * scale;
    float line_height = font_size * 1.5f;
    float pad_top = style.padding_top * scale;
    float pad_left = style.padding_left * scale;
    float margin_top = style.margin_top * scale;
    float margin_out_left = origin.x + pad_left;
    std::vector<std::vector<GlyphRunLayout>> runs;
    std::vector<GlyphRunLayout> line_runs;
    float cur_x = margin_out_left;
    float max_width = viewport_width - origin.x - pad_left - style.padding_right * scale;

    for (const auto& it : rb.inline_items) {
        if (it.kind == InlineRenderItem::Kind::Text) {
            auto shape = measurer.measure(it.text, font_size, it.style);
            if (cur_x + shape.width > max_width && cur_x > margin_out_left) {
                runs.push_back(std::move(line_runs));
                line_runs.clear();
                cur_x = pad_left;
            }
            GlyphRunLayout r;
            r.source_range = it.source_range; r.text = it.text; r.glyphs = std::move(shape.glyphs);
            r.style = it.style; r.origin = LogicalPoint(cur_x, 0.0f);
            r.width = shape.width; r.height = line_height; r.marker_visibility = MarkerVisibility::Always;
            cur_x += shape.width;
            line_runs.push_back(std::move(r));
        } else if (it.kind == InlineRenderItem::Kind::Math) {
            std::u32string mt = U"$" + it.text + U"$";
            auto shape = measurer.measure(mt, font_size, InlineStyle::plain());
            if (cur_x + shape.width > max_width && cur_x > margin_out_left) {
                runs.push_back(std::move(line_runs));
                line_runs.clear();
                cur_x = pad_left;
            }
            GlyphRunLayout r;
            r.source_range = it.source_range; r.text = mt; r.glyphs = std::move(shape.glyphs);
            r.style = InlineStyle::plain(); r.origin = LogicalPoint(cur_x, 0.0f);
            r.width = shape.width; r.height = line_height;
            r.marker_visibility = MarkerVisibility::Always;
            cur_x += shape.width; line_runs.push_back(std::move(r));
        } else if (it.kind == InlineRenderItem::Kind::Marker) {
            auto shape = measurer.measure(it.text, font_size, InlineStyle::plain());
            auto vis = marker_visibility_for(it.source_range, caret);
            float marker_width = (vis == MarkerVisibility::HiddenButEditable) ? 0.0f : shape.width;
            GlyphRunLayout r;
            r.source_range = it.source_range; r.text = it.text; r.glyphs = std::move(shape.glyphs);
            r.style = InlineStyle::plain(); r.origin = LogicalPoint(cur_x, 0.0f);
            r.width = marker_width; r.height = line_height; r.marker_visibility = vis;
            cur_x += marker_width;
            line_runs.push_back(std::move(r));
        }
        (void)it;
    }
    if (!line_runs.empty()) runs.push_back(std::move(line_runs));

    std::size_t total_lines = runs.empty() ? 1 : runs.size();
    float block_height = margin_top + style.margin_bottom * scale + pad_top + total_lines * line_height + style.padding_bottom * scale;
    LayoutBlock lb(rb.id, rb.source_range, {LayoutBlockKind::Paragraph}, style);
    lb.rect = LogicalRect(origin.x, y + margin_top, viewport_width, block_height - margin_top - style.margin_bottom * scale);
    for (std::size_t i = 0; i < runs.size(); ++i) {
        float line_y = y + margin_top + pad_top + i * line_height;
        CharRange lr{CharOffset(), CharOffset()};
        bool first = true;
        for (const auto& r : runs[i]) {
            if (first) { lr = r.source_range; first = false; }
            else {
                lr.start = (r.source_range.start < lr.start) ? r.source_range.start : lr.start;
                lr.end = (r.source_range.end > lr.end) ? r.source_range.end : lr.end;
            }
        }
        if (first) lr = rb.source_range;
        TextLineLayout ll{lr};
        ll.rect = LogicalRect(origin.x, line_y, viewport_width, line_height);
        ll.baseline = line_y + font_size;
        ll.runs = runs[i];
        LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
        lb.children.push_back(std::move(li));
    }
    return {std::move(lb), block_height};
}

inline std::pair<LayoutBlock, float> layout_blank_block(const RenderBlock& rb, float y, float viewport_width, float scale, LogicalPoint origin) {
    float line_height = 24.0f * scale;
    LayoutBlock block(rb.id, rb.source_range, {LayoutBlockKind::Blank}, rb.block_style);
    block.rect = LogicalRect(origin.x, y, viewport_width, line_height);
    TextLineLayout line{rb.content_range};
    line.rect = block.rect;
    line.baseline = y + 16.0f * scale;
    LayoutItem item;
    item.kind = LayoutItem::Kind::Line;
    item.line = std::move(line);
    block.children.push_back(std::move(item));
    return {std::move(block), line_height};
}

inline std::pair<LayoutBlock, float> layout_code_block(const RenderBlock& rb, float y, float viewport_width, float scale, TextMeasurer& measurer) {
    float font_size = 14.0f * scale;
    float line_height = font_size * 1.4f;
    const auto& style = rb.block_style;
    float pad = 12.0f * scale;
    std::vector<std::u32string> lines;
    std::u32string acc;
    for (char32_t c : rb.code_text + U"\n") {
        if (c == '\n') { lines.push_back(acc); acc.clear(); }
        else acc.push_back(c);
    }
    LayoutBlock lb(rb.id, rb.source_range, {LayoutBlockKind::CodeBlock}, style);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto shape = measurer.measure(lines[i], font_size, InlineStyle::plain());
        TextLineLayout ll{{}};
        ll.rect = LogicalRect(pad, y + style.margin_top * scale + pad + i * line_height, viewport_width - pad, line_height);
        ll.baseline = ll.rect.y + font_size;
        GlyphRunLayout r;
        r.source_range = rb.source_range; r.text = lines[i]; r.glyphs = std::move(shape.glyphs);
        r.style = InlineStyle::plain(); r.origin = LogicalPoint(pad, 0.0f);
        r.width = shape.width; r.height = line_height; r.marker_visibility = MarkerVisibility::Always;
        ll.runs.push_back(std::move(r));
        LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
        lb.children.push_back(std::move(li));
    }
    float h = style.margin_top * scale + pad + lines.size() * line_height + pad + style.margin_bottom * scale;
    lb.rect = LogicalRect(0.0f, y + style.margin_top * scale, viewport_width, h - style.margin_top * scale - style.margin_bottom * scale);
    return {std::move(lb), h};
}

inline std::pair<LayoutBlock, float> layout_math_block(const RenderBlock& rb, float y, float viewport_width, float scale, TextMeasurer& measurer) {
    float font_size = 14.0f * scale;
    float line_height = font_size * 1.5f;
    const auto& style = rb.block_style;
    float pad = style.padding_top * scale;
    std::vector<std::u32string> lines;
    std::u32string acc;
    for (char32_t c : rb.tex + U"\n") {
        if (c == '\n') { lines.push_back(acc); acc.clear(); }
        else acc.push_back(c);
    }
    if (lines.empty()) lines.push_back(U"");
    LayoutBlock lb(rb.id, rb.source_range, {LayoutBlockKind::MathBlock}, style);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto shape = measurer.measure(lines[i], font_size, InlineStyle::plain());
        TextLineLayout ll{{}};
        ll.rect = LogicalRect(pad, y + style.margin_top * scale + pad + i * line_height, viewport_width - pad, line_height);
        ll.baseline = ll.rect.y + font_size;
        GlyphRunLayout r;
        r.source_range = rb.source_range; r.text = lines[i]; r.glyphs = std::move(shape.glyphs);
        r.style = InlineStyle::plain(); r.origin = LogicalPoint(pad, 0.0f);
        r.width = shape.width; r.height = line_height; r.marker_visibility = MarkerVisibility::Always;
        ll.runs.push_back(std::move(r));
        LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
        lb.children.push_back(std::move(li));
    }
    float h = style.margin_top * scale + pad + lines.size() * line_height + pad + style.margin_bottom * scale;
    lb.rect = LogicalRect(0.0f, y + style.margin_top * scale, viewport_width, h - style.margin_top * scale - style.margin_bottom * scale);
    return {std::move(lb), h};
}

inline std::pair<LayoutBlock, float> layout_toc_block(const RenderBlock& rb, float y, float viewport_width, float scale, TextMeasurer& measurer, const Outline& outline) {
    float font_size = 14.0f * scale;
    float item_height = font_size * 1.5f;
    const auto& style = rb.block_style;
    auto flat = outline.flat_items();
    LayoutBlock lb(rb.id, rb.source_range, {LayoutBlockKind::Toc}, style);
    float h = style.margin_top * scale + flat.size() * item_height + style.margin_bottom * scale;
    lb.rect = LogicalRect(0.0f, y + style.margin_top * scale, viewport_width, h - style.margin_top * scale - style.margin_bottom * scale);
    for (std::size_t i = 0; i < flat.size(); ++i) {
        auto title = utf8_to_cps(flat[i]->title_plain_text);
        TextLineLayout ll{{}};
        ll.rect = LogicalRect(style.padding_left * scale, y + style.margin_top * scale + i * item_height, viewport_width, item_height);
        ll.baseline = ll.rect.y + font_size;
        auto shape = measurer.measure(title, font_size, InlineStyle::plain());
        GlyphRunLayout r;
        r.text = title; r.glyphs = std::move(shape.glyphs);
        r.style = InlineStyle::plain(); r.origin = LogicalPoint(style.padding_left * scale, 0.0f);
        r.width = shape.width; r.height = item_height;
        r.marker_visibility = MarkerVisibility::Always;
        ll.runs.push_back(std::move(r));
        LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
        lb.children.push_back(std::move(li));
    }
    return {std::move(lb), h};
}

inline std::pair<LayoutBlock, float> layout_unsupported(const RenderBlock& rb, float y, float viewport_width, float scale, TextMeasurer& measurer) {
    float font_size = 14.0f * scale;
    float line_height = font_size * 1.5f;
    const auto& style = rb.block_style;
    std::vector<std::u32string> lines;
    std::u32string acc; auto cps = utf8_to_cps(rb.raw);
    for (char32_t c : cps + U"\n") { if (c == '\n') { lines.push_back(acc); acc.clear(); } else acc.push_back(c); }
    LayoutBlock lb(rb.id, rb.source_range, {LayoutBlockKind::UnsupportedMarkup}, style);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto shape = measurer.measure(lines[i], font_size, InlineStyle::plain());
        TextLineLayout ll{{}};
        ll.rect = LogicalRect(style.padding_left * scale, y + style.margin_top * scale + i * line_height, viewport_width, line_height);
        ll.baseline = ll.rect.y + font_size;
        GlyphRunLayout r;
        r.text = lines[i]; r.glyphs = std::move(shape.glyphs);
        r.style = InlineStyle::plain(); r.origin = LogicalPoint(style.padding_left * scale, 0.0f);
        r.width = shape.width; r.height = line_height;
        r.marker_visibility = MarkerVisibility::Always;
        ll.runs.push_back(std::move(r));
        LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
        lb.children.push_back(std::move(li));
    }
    if (lines.empty()) lines.push_back(U"");
    float h = style.margin_top * scale + lines.size() * line_height + style.margin_bottom * scale;
    lb.rect = LogicalRect(0.0f, y, viewport_width, h);
    return {std::move(lb), h};
}

inline LayoutTree layout_blocks(const std::vector<RenderBlock>& blocks, float viewport_width,
                                float scale, TextMeasurer& measurer,
                                std::optional<CharOffset> caret, LogicalPoint origin,
                                const Outline& outline) {
    LayoutTree tree;
    float y = origin.y;
    for (const auto& rb : blocks) {
        std::pair<LayoutBlock, float> pr;
        switch (rb.kind) {
            case RenderBlockKind::Text:
                pr = layout_text_block(rb, y, viewport_width, scale, measurer, caret, origin);
                break;
            case RenderBlockKind::Blank:
                pr = layout_blank_block(rb, y, viewport_width, scale, origin);
                break;
            case RenderBlockKind::Code:
                pr = layout_code_block(rb, y, viewport_width, scale, measurer);
                break;
            case RenderBlockKind::Math:
                pr = layout_math_block(rb, y, viewport_width, scale, measurer);
                break;
case RenderBlockKind::Toc:
                pr = layout_toc_block(rb, y, viewport_width, scale, measurer, outline);
                break;
            case RenderBlockKind::Unsupported:
                pr = layout_unsupported(rb, y, viewport_width, scale, measurer);
                break;
            default: {
                LayoutBlock lb(rb.id, rb.source_range, {LayoutBlockKind::Paragraph}, rb.block_style);
                float h = rb.block_style.margin_top * scale + rb.block_style.margin_bottom * scale;
                lb.rect = LogicalRect(origin.x, y + rb.block_style.margin_top * scale, viewport_width, h);
                pr = {std::move(lb), h};
                break;
            }
        }
        tree.blocks.push_back(std::move(pr.first));
        y += pr.second;
    }
    tree.total_height = y;
    tree.viewport = LogicalRect(origin.x, origin.y, viewport_width, 0);
    return tree;
}

} // namespace elmd

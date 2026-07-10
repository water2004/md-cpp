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

inline std::pair<LayoutBlock, float> layout_thematic_break(const RenderBlock& rb, float y, float viewport_width, float scale, LogicalPoint origin) {
    auto height = 48.0f * scale;
    LayoutBlock block(rb.id, rb.source_range, {LayoutBlockKind::ThematicBreak}, rb.block_style);
    block.rect = LogicalRect(origin.x, y, viewport_width, height);
    return {std::move(block), height};
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

inline std::u32string table_cell_layout_text(const std::vector<InlineRenderItem>& items) {
    std::u32string text;
    for (const auto& item : items) {
        if (item.kind == InlineRenderItem::Kind::Math) {
            text.push_back(U'$');
            text += item.text;
            text.push_back(U'$');
        }
        else {
            text += item.text;
        }
    }
    return text;
}

inline CharRange table_cell_layout_range(const std::vector<InlineRenderItem>& items, CharRange fallback) {
    if (items.empty()) return fallback;
    CharRange range = items.front().source_range;
    for (const auto& item : items) {
        if (item.source_range.start < range.start) range.start = item.source_range.start;
        if (item.source_range.end > range.end) range.end = item.source_range.end;
    }
    return range;
}

inline std::pair<LayoutBlock, float> layout_table_block(const RenderBlock& rb, float y, float viewport_width, float scale, TextMeasurer& measurer, LogicalPoint origin) {
    const auto& style = rb.block_style;
    float font_size = 16.0f * scale;
    float line_height = font_size * 1.5f;
    float margin_top = style.margin_top * scale;
    float margin_bottom = style.margin_bottom * scale;
    float cell_padding = 8.0f * scale;
    std::size_t columns = (std::max)(std::size_t{1}, rb.column_count);
    std::size_t rows = (std::max)(std::size_t{1}, rb.row_count);
    std::vector<float> widths(columns, 48.0f * scale);
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            auto index = row * columns + column;
            if (index >= rb.table_cells.size()) continue;
            auto text = table_cell_layout_text(rb.table_cells[index]);
            auto shape = measurer.measure(text, font_size, InlineStyle::plain());
            widths[column] = (std::max)(widths[column], shape.width + cell_padding * 2.0f);
        }
    }
    float available = (std::max)(viewport_width - style.padding_left * scale - style.padding_right * scale, 1.0f);
    float total_width = 0.0f;
    for (auto width : widths) total_width += width;
    if (total_width > available) {
        float scale_down = available / total_width;
        for (auto& width : widths) width *= scale_down;
        total_width = available;
    }

    LayoutBlock block(rb.id, rb.source_range, {LayoutBlockKind::Table}, style);
    TableLayout table;
    table.id = rb.id;
    table.source_range = rb.source_range;
    table.rect = LogicalRect(origin.x + style.padding_left * scale, y + margin_top, total_width, rows * line_height);
    for (std::size_t column = 0; column < columns; ++column) {
        TableLayoutColumn layout_column;
        layout_column.width = widths[column];
        layout_column.alignment = column < rb.table_aligns.size() ? rb.table_aligns[column] : TableAlignment::None;
        table.columns.push_back(layout_column);
    }

    for (std::size_t row = 0; row < rows; ++row) {
        TableLayoutRow layout_row;
        layout_row.is_header = row == 0;
        layout_row.rect = LogicalRect(table.rect.x, table.rect.y + row * line_height, total_width, line_height);
        float x = table.rect.x;
        for (std::size_t column = 0; column < columns; ++column) {
            auto index = row * columns + column;
            const std::vector<InlineRenderItem>* items = index < rb.table_cells.size() ? &rb.table_cells[index] : nullptr;
            CharRange source_range = items ? table_cell_layout_range(*items, rb.content_range) : rb.content_range;
            TableLayoutCell cell;
            cell.source_range = source_range;
            cell.rect = LogicalRect(x, layout_row.rect.y, widths[column], line_height);
            TextLineLayout line(source_range);
            line.rect = cell.rect;
            line.baseline = line.rect.y + font_size;
            std::u32string text = items ? table_cell_layout_text(*items) : U"";
            InlineStyle text_style = InlineStyle::plain();
            if (row == 0) text_style.bold = true;
            auto shape = measurer.measure(text, font_size, text_style);
            float text_x = x + cell_padding;
            float inner_width = (std::max)(0.0f, widths[column] - cell_padding * 2.0f);
            auto alignment = column < rb.table_aligns.size() ? rb.table_aligns[column] : TableAlignment::None;
            if (alignment == TableAlignment::Right) text_x += (std::max)(0.0f, inner_width - shape.width);
            if (alignment == TableAlignment::Center) text_x += (std::max)(0.0f, (inner_width - shape.width) * 0.5f);
            GlyphRunLayout run;
            run.source_range = source_range;
            run.text = std::move(text);
            run.glyphs = std::move(shape.glyphs);
            run.style = text_style;
            run.origin = LogicalPoint(text_x, 0.0f);
            run.width = shape.width;
            run.height = line_height;
            run.marker_visibility = MarkerVisibility::Always;
            line.runs.push_back(std::move(run));
            LayoutItem line_item;
            line_item.kind = LayoutItem::Kind::Line;
            line_item.line = std::move(line);
            cell.content.push_back(std::move(line_item));
            layout_row.cells.push_back(std::move(cell));
            x += widths[column];
        }
        table.rows.push_back(std::move(layout_row));
    }
    block.rect = LogicalRect(origin.x, y + margin_top, viewport_width, rows * line_height);
    LayoutItem item;
    item.kind = LayoutItem::Kind::Table;
    item.table = std::move(table);
    block.children.push_back(std::move(item));
    float height = margin_top + rows * line_height + margin_bottom;
    return {std::move(block), height};
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
            case RenderBlockKind::Table:
                pr = layout_table_block(rb, y, viewport_width, scale, measurer, origin);
                break;
            case RenderBlockKind::ThematicBreak:
                pr = layout_thematic_break(rb, y, viewport_width, scale, origin);
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

// folia.core.block_layout — layout_blocks + helpers. Bakes viewport_origin into
// every rect/origin (single screen-coordinate system, HANDOFF invariant #1).
export module folia.core.block_layout;
import std;
import folia.core.types;
import folia.core.ids;
import folia.core.render_model;
import folia.core.text_measurer;
import folia.core.utf;
import folia.core.layout_tree;
import folia.core.outline;
import folia.core.selection;
import folia.core.text_edit;

export namespace folia {

inline MarkerVisibility marker_visibility_for(TextSpan span, std::optional<TextPosition> caret) {
    if (caret && caret->container_id == span.container_id && span.source_range.covers(caret->source_offset)) return MarkerVisibility::Always;
    return MarkerVisibility::HiddenButEditable;
}

inline std::pair<LayoutBlock, float> layout_text_block(const RenderBlock& rb, float y,
                                                       float viewport_width, float scale,
                                                       TextMeasurer& measurer,
                                                       std::optional<TextPosition> caret,
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

    auto flush_line = [&](bool force) {
        if (force || !line_runs.empty()) runs.push_back(std::move(line_runs));
        line_runs.clear();
        cur_x = margin_out_left;
    };
    auto append_piece = [&](
        std::u32string text,
        TextSpan span,
        InlineStyle inline_style,
        MarkerVisibility visibility,
        std::optional<TextAffinity> generated_boundary_affinity = std::nullopt) {
        if (text.empty()) return;
        auto shape = measurer.measure(text, font_size, inline_style);
        auto width = visibility == MarkerVisibility::HiddenButEditable ? 0.0f : shape.width;
        if (cur_x + width > max_width && cur_x > margin_out_left) flush_line(false);
        GlyphRunLayout run;
        run.source_span = span;
        run.text = std::move(text);
        run.glyphs = std::move(shape.glyphs);
        run.style = inline_style;
        run.origin = LogicalPoint(cur_x, 0.0f);
        run.width = width;
        run.height = line_height;
        run.marker_visibility = visibility;
        run.generated_boundary_affinity = generated_boundary_affinity;
        cur_x += width;
        line_runs.push_back(std::move(run));
    };
    auto append_text = [&](
        std::u32string text,
        TextSpan span,
        InlineStyle inline_style,
        MarkerVisibility visibility,
        std::optional<TextAffinity> generated_boundary_affinity = std::nullopt) {
        std::size_t start = 0;
        while (start < text.size()) {
            auto newline = text.find(U'\n', start);
            auto end = newline == std::u32string::npos ? text.size() : newline;
            auto piece_span = span;
            if (span.source_range.length() == text.size()) {
                piece_span.source_range.start += start;
                piece_span.source_range.end = span.source_range.start + end;
            }
            append_piece(
                text.substr(start, end - start),
                piece_span,
                inline_style,
                visibility,
                generated_boundary_affinity);
            if (newline == std::u32string::npos) break;
            auto newline_span = span;
            if (span.source_range.length() == text.size()) {
                newline_span.source_range.start = span.source_range.start + newline;
                newline_span.source_range.end = newline_span.source_range.start + 1;
            }
            append_piece(
                U"\n",
                newline_span,
                inline_style,
                MarkerVisibility::Always,
                generated_boundary_affinity);
            flush_line(true);
            start = newline + 1;
        }
    };

    for (const auto& it : rb.inline_items) {
        if (it.kind == InlineRenderItem::Kind::Text) {
            append_text(it.text, it.source_span, it.style, MarkerVisibility::Always);
        } else if (it.kind == InlineRenderItem::Kind::Math) {
            append_text(U"$" + it.text + U"$", it.source_span, InlineStyle::plain(), MarkerVisibility::Always);
        } else if (it.kind == InlineRenderItem::Kind::Marker) {
            auto text = it.special().display_text.empty() ? it.text : it.special().display_text;
            auto visibility = it.special().visibility == MarkerVisibility::Always
                ? MarkerVisibility::Always
                : marker_visibility_for(it.source_span, caret);
            append_text(
                std::move(text),
                it.source_span,
                it.style,
                visibility,
                it.special().generated_boundary_affinity);
        }
    }
    if (!line_runs.empty()) runs.push_back(std::move(line_runs));

    std::size_t total_lines = runs.empty() ? 1 : runs.size();
    float block_height = margin_top + style.margin_bottom * scale + pad_top + total_lines * line_height + style.padding_bottom * scale;
    LayoutBlock lb(rb.id, rb.source_span, {LayoutBlockKind::Paragraph}, style);
    lb.rect = LogicalRect(origin.x, y + margin_top, viewport_width, block_height - margin_top - style.margin_bottom * scale);
    for (std::size_t i = 0; i < runs.size(); ++i) {
        float line_y = y + margin_top + pad_top + i * line_height;
        TextSpan line_span{rb.id, {0, 0}};
        bool first = true;
        for (const auto& r : runs[i]) {
            if (first) { line_span = r.source_span; first = false; }
            else if (r.source_span.container_id == line_span.container_id) {
                line_span.source_range.start = (std::min)(line_span.source_range.start, r.source_span.source_range.start);
                line_span.source_range.end = (std::max)(line_span.source_range.end, r.source_span.source_range.end);
            }
        }
        TextLineLayout ll{line_span};
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
    LayoutBlock block(rb.id, rb.source_span, {LayoutBlockKind::Blank}, rb.block_style);
    block.rect = LogicalRect(origin.x, y, viewport_width, line_height);
    TextLineLayout line{TextSpan{rb.id, {0, 0}}};
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
    LayoutBlock block(rb.id, rb.source_span, {LayoutBlockKind::ThematicBreak}, rb.block_style);
    block.rect = LogicalRect(origin.x, y, viewport_width, height);
    return {std::move(block), height};
}

inline std::pair<LayoutBlock, float> layout_code_block(const RenderBlock& rb, float y, float viewport_width, float scale, TextMeasurer& measurer) {
    const auto& special = rb.special();
    float font_size = 14.0f * scale;
    float line_height = font_size * 1.4f;
    const auto& style = rb.block_style;
    float pad = 12.0f * scale;
    const auto lines = code_presentation_lines(special.code_text);
    LayoutBlock lb(rb.id, rb.source_span, {LayoutBlockKind::CodeBlock}, style);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto text = special.code_text.substr(
            lines[i].content_range.start,
            lines[i].content_range.length());
        const auto source_range = SourceRange{
            special.content_to_source.empty()
                ? lines[i].content_range.start
                : special.content_to_source[lines[i].content_range.start],
            special.content_to_source.empty()
                ? lines[i].content_range.end
                : special.content_to_source[lines[i].content_range.end]};
        auto shape = measurer.measure(text, font_size, InlineStyle::plain());
        TextLineLayout ll{{rb.id, source_range}};
        ll.rect = LogicalRect(pad, y + style.margin_top * scale + pad + i * line_height, viewport_width - pad, line_height);
        ll.baseline = ll.rect.y + font_size;
        GlyphRunLayout r;
        r.source_span = {rb.id, source_range}; r.text = text; r.glyphs = std::move(shape.glyphs);
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

inline TextSpan table_cell_layout_span(const std::vector<InlineRenderItem>& items, NodeId fallback_owner) {
    if (items.empty()) return {fallback_owner, {0, 0}};
    TextSpan span = items.front().source_span;
    for (const auto& item : items) {
        if (item.source_span.container_id != span.container_id) continue;
        span.source_range.start = (std::min)(span.source_range.start, item.source_span.source_range.start);
        span.source_range.end = (std::max)(span.source_range.end, item.source_span.source_range.end);
    }
    return span;
}

inline std::pair<LayoutBlock, float> layout_table_block(const RenderBlock& rb, float y, float viewport_width, float scale, TextMeasurer& measurer, LogicalPoint origin) {
    const auto& special = rb.special();
    const auto& style = rb.block_style;
    float font_size = 16.0f * scale;
    float line_height = font_size * 1.5f;
    float margin_top = style.margin_top * scale;
    float margin_bottom = style.margin_bottom * scale;
    float cell_padding = 8.0f * scale;
    std::size_t columns = (std::max)(std::size_t{1}, special.column_count);
    std::size_t rows = (std::max)(std::size_t{1}, special.row_count);
    std::vector<float> widths(columns, 48.0f * scale);
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            auto index = row * columns + column;
            if (index >= special.table_cells.size()) continue;
            auto text = table_cell_layout_text(special.table_cells[index]);
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

    LayoutBlock block(rb.id, rb.source_span, {LayoutBlockKind::Table}, style);
    TableLayout table;
    table.id = rb.id;
    table.source_span = rb.source_span;
    table.rect = LogicalRect(origin.x + style.padding_left * scale, y + margin_top, total_width, rows * line_height);
    for (std::size_t column = 0; column < columns; ++column) {
        TableLayoutColumn layout_column;
        layout_column.width = widths[column];
        layout_column.alignment = column < special.table_aligns.size() ? special.table_aligns[column] : TableAlignment::None;
        table.columns.push_back(layout_column);
    }

    for (std::size_t row = 0; row < rows; ++row) {
        TableLayoutRow layout_row;
        layout_row.is_header = row == 0;
        layout_row.rect = LogicalRect(table.rect.x, table.rect.y + row * line_height, total_width, line_height);
        float x = table.rect.x;
        for (std::size_t column = 0; column < columns; ++column) {
            auto index = row * columns + column;
            const std::vector<InlineRenderItem>* items = index < special.table_cells.size() ? &special.table_cells[index] : nullptr;
            TextSpan source_span = items ? table_cell_layout_span(*items, rb.id) : TextSpan{rb.id, {0, 0}};
            TableLayoutCell cell;
            cell.source_span = source_span;
            cell.rect = LogicalRect(x, layout_row.rect.y, widths[column], line_height);
            TextLineLayout line(source_span);
            line.rect = cell.rect;
            line.baseline = line.rect.y + font_size;
            std::u32string text = items ? table_cell_layout_text(*items) : U"";
            InlineStyle text_style = InlineStyle::plain();
            if (row == 0) text_style.bold = true;
            auto shape = measurer.measure(text, font_size, text_style);
            float text_x = x + cell_padding;
            float inner_width = (std::max)(0.0f, widths[column] - cell_padding * 2.0f);
            auto alignment = column < special.table_aligns.size() ? special.table_aligns[column] : TableAlignment::None;
            if (alignment == TableAlignment::Right) text_x += (std::max)(0.0f, inner_width - shape.width);
            if (alignment == TableAlignment::Center) text_x += (std::max)(0.0f, (inner_width - shape.width) * 0.5f);
            GlyphRunLayout run;
            run.source_span = source_span;
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
    const auto& special = rb.special();
    float font_size = 14.0f * scale;
    float line_height = font_size * 1.5f;
    const auto& style = rb.block_style;
    float pad = style.padding_top * scale;
    std::vector<SourceRange> lines;
    std::size_t line_start = 0;
    while (true) {
        const auto newline = special.tex.find(U'\n', line_start);
        const auto line_end = newline == std::u32string::npos ? special.tex.size() : newline;
        lines.push_back({line_start, line_end});
        if (newline == std::u32string::npos) break;
        line_start = newline + 1;
    }
    LayoutBlock lb(rb.id, rb.source_span, {LayoutBlockKind::MathBlock}, style);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto text = special.tex.substr(lines[i].start, lines[i].length());
        const auto source_range = SourceRange{
            special.content_to_source.empty() ? lines[i].start : special.content_to_source[lines[i].start],
            special.content_to_source.empty() ? lines[i].end : special.content_to_source[lines[i].end]};
        auto shape = measurer.measure(text, font_size, InlineStyle::plain());
        TextLineLayout ll{{rb.id, source_range}};
        ll.rect = LogicalRect(pad, y + style.margin_top * scale + pad + i * line_height, viewport_width - pad, line_height);
        ll.baseline = ll.rect.y + font_size;
        GlyphRunLayout r;
        r.source_span = {rb.id, source_range}; r.text = text; r.glyphs = std::move(shape.glyphs);
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
    LayoutBlock lb(rb.id, rb.source_span, {LayoutBlockKind::Toc}, style);
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
    std::u32string acc; auto cps = utf8_to_cps(rb.special().raw);
    for (char32_t c : cps + U"\n") { if (c == '\n') { lines.push_back(acc); acc.clear(); } else acc.push_back(c); }
    LayoutBlock lb(rb.id, rb.source_span, {LayoutBlockKind::UnsupportedMarkup}, style);
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
                                std::optional<TextPosition> caret, LogicalPoint origin,
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
            case RenderBlockKind::Quote:
            case RenderBlockKind::Callout:
            case RenderBlockKind::Footnote:
                pr = layout_text_block(rb, y, viewport_width, scale, measurer, caret, origin);
                if (rb.kind == RenderBlockKind::Quote) pr.first.kind.kind = LayoutBlockKind::Quote;
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
                LayoutBlock lb(rb.id, rb.source_span, {LayoutBlockKind::Paragraph}, rb.block_style);
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

} // namespace folia

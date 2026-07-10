export module elmd.core.source_structure;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.document;
import elmd.core.source_map;

export namespace elmd {

enum class SourceBlockKind {
    Semantic,
    Blank,
};

struct SourceBlockSpan {
    SourceBlockKind kind = SourceBlockKind::Semantic;
    CharRange source_range;
    CharRange content_range;
    std::optional<std::size_t> document_block_index;
    std::optional<NodeId> node_id;
};

struct SourceStructure {
    std::vector<SourceBlockSpan> blocks;
    std::vector<CharRange> separators;
};

struct QuoteSourceLine {
    CharRange source_range;
    CharRange content_range;
    std::vector<CharRange> marker_ranges;
    bool empty = false;
    bool hard_break_from_previous = false;
};

inline std::optional<QuoteSourceLine> quote_source_line_at(std::u32string_view text, CharOffset offset) {
    auto position = (std::min)(offset.v, text.size());
    auto line_start = position;
    while (line_start > 0 && text[line_start - 1] != U'\n') --line_start;
    auto line_end = position;
    while (line_end < text.size() && text[line_end] != U'\n') ++line_end;
    auto source_end = line_end < text.size() ? line_end + 1 : line_end;
    auto cursor = line_start;
    while (cursor < line_end && cursor - line_start < 3 && text[cursor] == U' ') ++cursor;
    QuoteSourceLine line;
    line.source_range = CharRange(CharOffset(line_start), CharOffset(source_end));
    while (cursor < line_end && text[cursor] == U'>') {
        auto marker_start = cursor++;
        if (cursor < line_end && text[cursor] == U' ') ++cursor;
        line.marker_ranges.push_back(CharRange(CharOffset(marker_start), CharOffset(cursor)));
    }
    if (line.marker_ranges.empty()) return std::nullopt;
    line.content_range = CharRange(CharOffset(cursor), CharOffset(line_end));
    line.empty = true;
    for (auto index = cursor; index < line_end; ++index) if (text[index] != U' ' && text[index] != U'\t') line.empty = false;
    line.hard_break_from_previous = line_start >= 3 && text[line_start - 1] == U'\n' && text[line_start - 2] == U' ' && text[line_start - 3] == U' ';
    return line;
}

inline std::vector<SourceBlockSpan> blank_lines_in_range(std::u32string_view text, std::size_t start, std::size_t end) {
    std::vector<SourceBlockSpan> lines;
    start = (std::min)(start, text.size());
    end = (std::min)((std::max)(end, start), text.size());
    std::size_t pos = start;
    while (pos < end) {
        std::size_t line_start = pos;
        while (pos < end && text[pos] != U'\n') ++pos;
        std::size_t content_end = pos;
        if (pos < end && text[pos] == U'\n') ++pos;
        SourceBlockSpan line;
        line.kind = SourceBlockKind::Blank;
        line.source_range = CharRange(CharOffset(line_start), CharOffset(pos));
        line.content_range = CharRange(CharOffset(line_start), CharOffset(content_end));
        lines.push_back(std::move(line));
    }
    return lines;
}

inline SourceBlockSpan terminal_blank_at(std::size_t offset) {
    SourceBlockSpan line;
    line.kind = SourceBlockKind::Blank;
    line.source_range = CharRange(CharOffset(offset), CharOffset(offset));
    line.content_range = line.source_range;
    return line;
}

inline SourceStructure build_source_structure(const MarkdownDocument& document, std::u32string_view text) {
    SourceStructure structure;
    std::vector<SourceBlockSpan> semantic;
    semantic.reserve(document.blocks.size());
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        const auto& block = document.blocks[index];
        const auto* range = document.source_map.find_node_by_id(block.id);
        if (!range) continue;
        SourceBlockSpan span;
        span.kind = SourceBlockKind::Semantic;
        span.source_range = range->source_range;
        span.content_range = range->content_range;
        span.document_block_index = index;
        span.node_id = block.id;
        semantic.push_back(std::move(span));
    }

    if (semantic.empty()) {
        auto lines = blank_lines_in_range(text, 0, text.size());
        if (!lines.empty()) {
            structure.separators.push_back(lines.front().source_range);
            for (std::size_t index = 1; index < lines.size(); ++index) structure.blocks.push_back(std::move(lines[index]));
        }
        if (text.empty() || text.back() == U'\n') structure.blocks.push_back(terminal_blank_at(text.size()));
        return structure;
    }

    auto append_gap_before_semantic = [&](std::size_t start, std::size_t end) {
        auto lines = blank_lines_in_range(text, start, end);
        if (lines.empty()) return;
        structure.separators.push_back(lines.back().source_range);
        lines.pop_back();
        for (auto& line : lines) structure.blocks.push_back(std::move(line));
    };

    append_gap_before_semantic(0, semantic.front().source_range.start.v);
    structure.blocks.push_back(semantic.front());
    for (std::size_t index = 1; index < semantic.size(); ++index) {
        append_gap_before_semantic(semantic[index - 1].source_range.end.v, semantic[index].source_range.start.v);
        structure.blocks.push_back(semantic[index]);
    }

    auto trailing = blank_lines_in_range(text, semantic.back().source_range.end.v, text.size());
    if (!trailing.empty()) {
        structure.separators.push_back(trailing.front().source_range);
        for (std::size_t index = 1; index < trailing.size(); ++index) structure.blocks.push_back(std::move(trailing[index]));
        if (!text.empty() && text.back() == U'\n') structure.blocks.push_back(terminal_blank_at(text.size()));
    }
    return structure;
}

inline const SourceBlockSpan* source_blank_at(const SourceStructure& structure, CharOffset offset) {
    for (const auto& block : structure.blocks) {
        if (block.kind != SourceBlockKind::Blank) continue;
        if (block.content_range.contains(offset) || block.content_range.start == offset) return &block;
    }
    return nullptr;
}

inline const SourceBlockSpan* source_semantic_at(const SourceStructure& structure, CharOffset offset) {
    for (const auto& block : structure.blocks) {
        if (block.kind != SourceBlockKind::Semantic) continue;
        if (block.content_range.start <= offset && offset <= block.content_range.end) return &block;
    }
    for (const auto& block : structure.blocks) {
        if (block.kind == SourceBlockKind::Semantic && block.source_range.contains(offset)) return &block;
    }
    return nullptr;
}

}

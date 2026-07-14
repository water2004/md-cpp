// elmd.core.source_editor — authoritative plain-source editing session.
//
// This is deliberately separate from EditorDocument. While source mode is
// active, one flat lossless source buffer and one flat source selection are
// authoritative. The block tree is rebuilt only when leaving source mode.
export module elmd.core.source_editor;
import std;
import elmd.core.ids;
import elmd.core.text_edit;
import elmd.core.selection;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.source_style;
import elmd.core.utf;

export namespace elmd {

struct SourceSelection {
    std::size_t anchor = 0;
    std::size_t active = 0;
    TextAffinity anchor_affinity = TextAffinity::Downstream;
    TextAffinity active_affinity = TextAffinity::Downstream;

    static SourceSelection caret(std::size_t offset, TextAffinity affinity = TextAffinity::Downstream) {
        return {offset, offset, affinity, affinity};
    }
    bool is_caret() const { return anchor == active; }
    SourceRange ordered_range() const { return {(std::min)(anchor, active), (std::max)(anchor, active)}; }
    bool operator==(SourceSelection const&) const = default;
};

struct SourceLineState {
    char32_t fence_character = U'\0';
    std::size_t fence_length = 0;
    std::string language;

    bool in_fence() const { return fence_character != U'\0'; }
    bool operator==(SourceLineState const&) const = default;
};

struct SourceLine {
    NodeId id{};
    std::size_t source_start = 0;
    std::u32string text;
    bool has_newline = false;
    SourceLineState state_before;
    SourceLineState state_after;
    std::vector<SourceStyleSpan> styles;
    bool code_content = false;
    std::optional<std::string> code_language;
    std::uint64_t presentation_key = 0;

    std::size_t source_end() const { return source_start + text.size(); }
};

struct SourceEditTransaction {
    SourceRange range_before;
    std::u32string inserted;
    std::u32string removed;
    SourceSelection selection_before;
    SourceSelection selection_after;
};

namespace source_editor_detail {

inline void add_style(std::vector<SourceStyleSpan>& styles, SourceRange range, SourceSyntaxKind kind) {
    if (range.empty()) return;
    styles.push_back({range, kind});
}

inline void style_delimited(
    std::vector<SourceStyleSpan>& styles,
    InlineCstNode const& node,
    SourceSyntaxKind content_kind) {
    add_style(styles, node.delim.opening, SourceSyntaxKind::Marker);
    add_style(styles, node.delim.content, content_kind);
    if (node.delim.closing) add_style(styles, *node.delim.closing, SourceSyntaxKind::Marker);
}

inline void collect_inline_styles(std::vector<SourceStyleSpan>& styles, InlineCstNodes const& nodes) {
    for (auto const& node : nodes) {
        using K = InlineCstKind;
        switch (node.kind) {
            case K::Strong: style_delimited(styles, node, SourceSyntaxKind::Strong); break;
            case K::Emphasis: style_delimited(styles, node, SourceSyntaxKind::Emphasis); break;
            case K::Strikethrough: style_delimited(styles, node, SourceSyntaxKind::Strikethrough); break;
            case K::CodeSpan: style_delimited(styles, node, SourceSyntaxKind::Code); break;
            case K::InlineMath: style_delimited(styles, node, SourceSyntaxKind::Math); break;
            case K::Link:
            case K::Autolink:
            case K::WikiLink:
            case K::FootnoteRef:
            case K::Image:
                add_style(styles, node.range, SourceSyntaxKind::Link);
                break;
            case K::Escape: add_style(styles, node.range, SourceSyntaxKind::Escape); break;
            case K::Entity: add_style(styles, node.range, SourceSyntaxKind::Entity); break;
            case K::Error:
            case K::Incomplete:
                add_style(styles, node.range, SourceSyntaxKind::Error);
                break;
            default: break;
        }
        collect_inline_styles(styles, node.children);
    }
}

inline std::size_t leading_spaces(std::u32string_view line) {
    std::size_t count = 0;
    while (count < line.size() && count < 3 && line[count] == U' ') ++count;
    return count;
}

inline std::optional<std::pair<char32_t, std::size_t>> fence_at(std::u32string_view line) {
    auto start = leading_spaces(line);
    if (start >= line.size() || (line[start] != U'`' && line[start] != U'~')) return std::nullopt;
    auto end = start;
    while (end < line.size() && line[end] == line[start]) ++end;
    if (end - start < 3) return std::nullopt;
    return std::pair{line[start], end - start};
}

inline std::string fence_language(std::u32string_view line, std::size_t fence_length) {
    auto cursor = leading_spaces(line) + fence_length;
    while (cursor < line.size() && (line[cursor] == U' ' || line[cursor] == U'\t')) ++cursor;
    auto end = cursor;
    while (end < line.size() && line[end] != U' ' && line[end] != U'\t') ++end;
    return cps_to_utf8(line.substr(cursor, end - cursor));
}

inline bool closes_fence(std::u32string_view line, SourceLineState const& state) {
    auto candidate = fence_at(line);
    if (!candidate || candidate->first != state.fence_character || candidate->second < state.fence_length) return false;
    auto cursor = leading_spaces(line) + candidate->second;
    return std::all_of(line.begin() + static_cast<std::ptrdiff_t>(cursor), line.end(), [](char32_t value) {
        return value == U' ' || value == U'\t';
    });
}

inline void add_block_prefix_styles(std::u32string_view line, std::vector<SourceStyleSpan>& styles) {
    auto cursor = leading_spaces(line);
    auto quote_start = cursor;
    while (cursor < line.size() && line[cursor] == U'>') {
        ++cursor;
        if (cursor < line.size() && line[cursor] == U' ') ++cursor;
    }
    if (cursor > quote_start) add_style(styles, {quote_start, cursor}, SourceSyntaxKind::Marker);

    auto structural = cursor;
    if (cursor < line.size() && line[cursor] == U'#') {
        auto end = cursor;
        while (end < line.size() && line[end] == U'#' && end - cursor < 6) ++end;
        if (end == line.size() || line[end] == U' ' || line[end] == U'\t') {
            add_style(styles, {cursor, end}, SourceSyntaxKind::Marker);
            add_style(styles, {end, line.size()}, SourceSyntaxKind::Heading);
            cursor = end;
        }
    }
    if (structural < line.size() && (line[structural] == U'-' || line[structural] == U'+' || line[structural] == U'*')
        && structural + 1 < line.size() && (line[structural + 1] == U' ' || line[structural + 1] == U'\t')) {
        auto end = structural + 2;
        if (end + 2 < line.size() && line[end] == U'['
            && (line[end + 1] == U' ' || line[end + 1] == U'x' || line[end + 1] == U'X')
            && line[end + 2] == U']') end += 3;
        add_style(styles, {structural, end}, SourceSyntaxKind::Marker);
    } else {
        auto end = structural;
        while (end < line.size() && line[end] >= U'0' && line[end] <= U'9') ++end;
        if (end > structural && end < line.size() && (line[end] == U'.' || line[end] == U')')
            && end + 1 < line.size() && (line[end + 1] == U' ' || line[end + 1] == U'\t')) {
            add_style(styles, {structural, end + 2}, SourceSyntaxKind::Marker);
        }
    }
    if (structural + 3 < line.size() && line[structural] == U'[' && line[structural + 1] == U'^') {
        auto close = line.find(U"]:", structural + 2);
        if (close != std::u32string_view::npos) add_style(styles, {structural, close + 2}, SourceSyntaxKind::Marker);
    }
}

inline std::uint64_t hash_line(SourceLine const& line) {
    constexpr std::uint64_t offset = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto value = offset;
    auto mix = [&](std::uint64_t next) { value ^= next; value *= prime; };
    for (auto character : line.text) mix(static_cast<std::uint32_t>(character));
    mix(line.has_newline);
    mix(static_cast<std::uint32_t>(line.state_before.fence_character));
    mix(line.state_before.fence_length);
    for (auto character : line.state_before.language) mix(static_cast<unsigned char>(character));
    for (auto const& style : line.styles) {
        mix(style.range.start); mix(style.range.end); mix(static_cast<std::uint32_t>(style.kind));
    }
    return value;
}

inline void style_line(SourceLine& line, SourceLineState& state) {
    line.state_before = state;
    line.styles.clear();
    line.code_content = false;
    line.code_language.reset();

    if (state.in_fence()) {
        if (closes_fence(line.text, state)) {
            add_style(line.styles, {leading_spaces(line.text), line.text.size()}, SourceSyntaxKind::Marker);
            state = {};
        } else {
            add_style(line.styles, {0, line.text.size()}, SourceSyntaxKind::Code);
            line.code_content = true;
            if (!state.language.empty()) line.code_language = state.language;
        }
    } else if (auto fence = fence_at(line.text)) {
        auto marker_start = leading_spaces(line.text);
        auto marker_end = marker_start + fence->second;
        add_style(line.styles, {marker_start, marker_end}, SourceSyntaxKind::Marker);
        add_style(line.styles, {marker_end, line.text.size()}, SourceSyntaxKind::Link);
        state.fence_character = fence->first;
        state.fence_length = fence->second;
        state.language = fence_language(line.text, fence->second);
    } else {
        add_block_prefix_styles(line.text, line.styles);
        InlineDocument inline_document;
        inline_document.source = line.text;
        std::uint64_t next_inline_id = 1;
        InlineParseContext context;
        context.allocate_id = [&] { return NodeId{next_inline_id++}; };
        reparse_inline_document(inline_document, context);
        collect_inline_styles(line.styles, inline_document.tree.nodes);
    }

    line.state_after = state;
    std::ranges::sort(line.styles, [](auto const& left, auto const& right) {
        if (left.range.start != right.range.start) return left.range.start < right.range.start;
        if (left.range.end != right.range.end) return left.range.end < right.range.end;
        return left.kind < right.kind;
    });
    line.styles.erase(std::unique(line.styles.begin(), line.styles.end()), line.styles.end());
    line.presentation_key = hash_line(line);
}

} // namespace source_editor_detail

class SourceEditor {
public:
    SourceEditor() { rebuild_lines_({}); }
    explicit SourceEditor(std::u32string source) : source_(std::move(source)) { rebuild_lines_({}); }

    std::u32string const& source() const { return source_; }
    std::vector<SourceLine> const& lines() const { return lines_; }
    SourceSelection selection() const { return selection_; }
    std::uint64_t revision() const { return revision_; }
    bool dirty() const { return !committed_edits_.empty(); }
    bool has_undo() const { return !undo_.empty(); }
    bool has_redo() const { return !redo_.empty(); }
    std::vector<SourceEditTransaction> const& committed_edits() const { return committed_edits_; }

    void set_selection(SourceSelection selection) {
        selection.anchor = (std::min)(selection.anchor, source_.size());
        selection.active = (std::min)(selection.active, source_.size());
        selection_ = selection;
    }

    std::u32string selected_text() const {
        auto range = selection_.ordered_range();
        return source_.substr(range.start, range.length());
    }

    bool replace_selection(std::u32string replacement) {
        return replace(selection_.ordered_range(), std::move(replacement));
    }

    bool replace(SourceRange range, std::u32string replacement) {
        if (!range.valid_for(source_.size())) return false;
        SourceEditTransaction transaction;
        transaction.range_before = range;
        transaction.inserted = std::move(replacement);
        transaction.removed = source_.substr(range.start, range.length());
        transaction.selection_before = selection_;
        auto target = range.start + transaction.inserted.size();
        transaction.selection_after = SourceSelection::caret(target);
        apply_(transaction, true);
        undo_.push_back(transaction);
        committed_edits_.push_back(transaction);
        redo_.clear();
        return true;
    }

    bool insert_text(std::u32string_view text) { return replace_selection(std::u32string{text}); }
    bool insert_newline() { return replace_selection(U"\n"); }

    bool delete_backward() {
        if (!selection_.is_caret()) return replace_selection({});
        if (selection_.active == 0) return false;
        return replace({selection_.active - 1, selection_.active}, {});
    }

    bool delete_forward() {
        if (!selection_.is_caret()) return replace_selection({});
        if (selection_.active >= source_.size()) return false;
        return replace({selection_.active, selection_.active + 1}, {});
    }

    bool undo() {
        if (undo_.empty()) return false;
        auto transaction = undo_.back();
        undo_.pop_back();
        apply_(transaction, false);
        redo_.push_back(std::move(transaction));
        if (!committed_edits_.empty()) committed_edits_.pop_back();
        return true;
    }

    bool redo() {
        if (redo_.empty()) return false;
        auto transaction = redo_.back();
        redo_.pop_back();
        apply_(transaction, true);
        undo_.push_back(transaction);
        committed_edits_.push_back(std::move(transaction));
        return true;
    }

    void select_all() { selection_ = {0, source_.size(), TextAffinity::Downstream, TextAffinity::Upstream}; }
    void move_left(bool extend = false) { move_to_(selection_.active == 0 ? 0 : selection_.active - 1, extend); }
    void move_right(bool extend = false) { move_to_((std::min)(source_.size(), selection_.active + 1), extend); }
    void move_document_start(bool extend = false) { move_to_(0, extend); }
    void move_document_end(bool extend = false) { move_to_(source_.size(), extend); }

    void move_line_start(bool extend = false) {
        auto const* line = line_for_offset_(selection_.active);
        move_to_(line ? line->source_start : 0, extend);
    }

    void move_line_end(bool extend = false) {
        auto const* line = line_for_offset_(selection_.active);
        move_to_(line ? line->source_end() : source_.size(), extend);
    }

    TextPosition position_from_source_offset(
        std::size_t offset,
        TextAffinity affinity = TextAffinity::Downstream) const {
        offset = (std::min)(offset, source_.size());
        auto const* line = line_for_offset_(offset);
        if (!line) return {};
        return {line->id, offset - line->source_start, affinity};
    }

    std::optional<std::size_t> source_offset_from_position(TextPosition position) const {
        auto found = std::find_if(lines_.begin(), lines_.end(), [&](auto const& line) { return line.id == position.container_id; });
        if (found == lines_.end()) return std::nullopt;
        return found->source_start + (std::min)(position.source_offset, found->text.size());
    }

    TextSelection projected_selection() const {
        return {
            position_from_source_offset(selection_.anchor, selection_.anchor_affinity),
            position_from_source_offset(selection_.active, selection_.active_affinity),
        };
    }

private:
    std::u32string source_;
    std::vector<SourceLine> lines_;
    SourceSelection selection_{};
    std::vector<SourceEditTransaction> undo_;
    std::vector<SourceEditTransaction> redo_;
    std::vector<SourceEditTransaction> committed_edits_;
    std::uint64_t revision_ = 1;
    std::uint64_t next_line_id_ = 0xf000000000000001ull;

    void move_to_(std::size_t offset, bool extend) {
        offset = (std::min)(offset, source_.size());
        if (extend) {
            selection_.active = offset;
            selection_.active_affinity = TextAffinity::Downstream;
        } else {
            selection_ = SourceSelection::caret(offset);
        }
    }

    SourceLine const* line_for_offset_(std::size_t offset) const {
        if (lines_.empty()) return nullptr;
        offset = (std::min)(offset, source_.size());
        auto found = std::upper_bound(lines_.begin(), lines_.end(), offset, [](std::size_t value, SourceLine const& line) {
            return value < line.source_start;
        });
        if (found == lines_.begin()) return &lines_.front();
        --found;
        if (found->has_newline && offset > found->source_end() && found + 1 != lines_.end()) ++found;
        return &*found;
    }

    void apply_(SourceEditTransaction const& transaction, bool forward) {
        auto old_lines = lines_;
        if (forward) {
            source_.replace(transaction.range_before.start, transaction.removed.size(), transaction.inserted);
            selection_ = transaction.selection_after;
        } else {
            source_.replace(transaction.range_before.start, transaction.inserted.size(), transaction.removed);
            selection_ = transaction.selection_before;
        }
        ++revision_;
        rebuild_lines_(std::move(old_lines));
    }

    void rebuild_lines_(std::vector<SourceLine> old_lines) {
        std::vector<SourceLine> fresh;
        std::size_t start = 0;
        while (true) {
            auto newline = source_.find(U'\n', start);
            SourceLine line;
            line.source_start = start;
            line.has_newline = newline != std::u32string::npos;
            auto end = line.has_newline ? newline : source_.size();
            line.text = source_.substr(start, end - start);
            fresh.push_back(std::move(line));
            if (!line.has_newline) break;
            start = newline + 1;
        }

        std::size_t prefix = 0;
        while (prefix < fresh.size() && prefix < old_lines.size()
            && fresh[prefix].text == old_lines[prefix].text
            && fresh[prefix].has_newline == old_lines[prefix].has_newline) {
            fresh[prefix].id = old_lines[prefix].id;
            ++prefix;
        }
        std::size_t suffix = 0;
        while (suffix + prefix < fresh.size() && suffix + prefix < old_lines.size()) {
            auto fresh_index = fresh.size() - suffix - 1;
            auto old_index = old_lines.size() - suffix - 1;
            if (fresh[fresh_index].text != old_lines[old_index].text
                || fresh[fresh_index].has_newline != old_lines[old_index].has_newline) break;
            fresh[fresh_index].id = old_lines[old_index].id;
            ++suffix;
        }
        if (prefix < fresh.size() - suffix && prefix < old_lines.size() - suffix) {
            fresh[prefix].id = old_lines[prefix].id;
        }
        for (auto& line : fresh) if (line.id.v == 0) line.id = NodeId{next_line_id_++};

        std::unordered_map<std::uint64_t, SourceLine const*> old_by_id;
        old_by_id.reserve(old_lines.size());
        for (auto const& line : old_lines) old_by_id.emplace(line.id.v, &line);
        SourceLineState state;
        for (auto& line : fresh) {
            auto previous = old_by_id.find(line.id.v);
            if (previous != old_by_id.end()
                && previous->second->text == line.text
                && previous->second->has_newline == line.has_newline
                && previous->second->state_before == state) {
                auto start_offset = line.source_start;
                line = *previous->second;
                line.source_start = start_offset;
                state = line.state_after;
            } else {
                source_editor_detail::style_line(line, state);
            }
        }
        lines_ = std::move(fresh);
    }
};

} // namespace elmd

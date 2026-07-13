// elmd.core.text_edit — block-local source coordinates and source edits.
export module elmd.core.text_edit;
import std;
import elmd.core.ids;
import elmd.core.selection;

export namespace elmd {

// Half-open [start, end) range in an editable node's block-local source.
struct SourceRange {
    std::size_t start = 0;
    std::size_t end = 0;

    constexpr std::size_t length() const { return end >= start ? end - start : 0; }
    constexpr bool empty() const { return start == end; }
    constexpr bool contains(std::size_t offset) const { return start <= offset && offset < end; }
    constexpr bool covers(std::size_t offset) const { return start <= offset && offset <= end; }
    constexpr bool valid_for(std::size_t source_length) const {
        return start <= end && end <= source_length;
    }
    constexpr static SourceRange empty_at(std::size_t offset) { return {offset, offset}; }
    bool operator==(const SourceRange&) const = default;
};

// The only authoritative coordinate inside editable block-local content.
struct TextPosition {
    NodeId container_id{};
    std::size_t source_offset = 0;
    TextAffinity affinity = TextAffinity::Downstream;

    bool operator==(const TextPosition&) const = default;
};

// A render/layout range in the same single coordinate system as selection.
struct TextSpan {
    NodeId container_id{};
    SourceRange source_range;

    bool operator==(const TextSpan&) const = default;
};

struct TextSelection {
    TextPosition anchor;
    TextPosition active;

    static TextSelection caret(TextPosition position) { return {position, position}; }
    bool is_caret() const { return anchor == active; }
    const TextPosition& head() const { return active; }
    const TextPosition& tail() const { return anchor; }
    bool operator==(const TextSelection&) const = default;
};

// Replace range in exactly one editable content node's source.
struct TextEdit {
    NodeId container_id{};
    SourceRange range;
    std::u32string replacement;
};

// A block-local source edit together with its exact inverse. Inline owners
// reparse their CST after applying it; code and math owners edit their local
// source directly. History uses the same operation shape for both.
struct AppliedSourceEdit {
    TextEdit forward;
    TextEdit inverse;
};

inline std::ptrdiff_t text_edit_delta(const TextEdit& edit) {
    return static_cast<std::ptrdiff_t>(edit.replacement.size())
        - static_cast<std::ptrdiff_t>(edit.range.length());
}

// Invalid ranges indicate a caller bug. Silently clamping them would make
// selection/history restoration non-reversible, so reject them explicitly.
inline std::ptrdiff_t apply_text_edit(std::u32string& source, const TextEdit& edit) {
    if (!edit.range.valid_for(source.size())) {
        throw std::out_of_range("TextEdit range is outside its block-local source");
    }
    source.replace(edit.range.start, edit.range.length(), edit.replacement);
    return text_edit_delta(edit);
}

// Translate an old source offset across an edit. Affinity resolves positions
// on or inside the replaced range: Upstream stays before replacement and
// Downstream moves after replacement. The old end boundary remains after the
// replacement because it was already on the following side of the edit.
inline std::size_t translate_offset(
    std::size_t offset,
    TextAffinity affinity,
    const TextEdit& edit) {
    const auto start = edit.range.start;
    const auto end = edit.range.end;
    const auto inserted_end = start + edit.replacement.size();

    if (offset < start) return offset;
    if (offset > end) {
        const auto shifted = static_cast<std::ptrdiff_t>(offset) + text_edit_delta(edit);
        return shifted < 0 ? 0 : static_cast<std::size_t>(shifted);
    }
    if (offset == end && end != start) return inserted_end;
    return affinity == TextAffinity::Upstream ? start : inserted_end;
}

inline TextPosition translate_position(TextPosition position, const TextEdit& edit) {
    if (position.container_id != edit.container_id) return position;
    position.source_offset = translate_offset(position.source_offset, position.affinity, edit);
    return position;
}

inline TextSelection translate_selection(TextSelection selection, const TextEdit& edit) {
    selection.anchor = translate_position(selection.anchor, edit);
    selection.active = translate_position(selection.active, edit);
    return selection;
}

} // namespace elmd

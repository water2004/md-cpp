export module elmd.core.callout;
import std;

export namespace elmd {

enum class CalloutVisualKind {
    Note,
    Tip,
    Warning,
};

inline std::optional<std::string> normalize_callout_kind(std::string_view kind) {
    std::string normalized;
    normalized.reserve(kind.size());
    for (const auto ch : kind) {
        normalized.push_back(ch >= 'a' && ch <= 'z'
            ? static_cast<char>(ch - 'a' + 'A')
            : ch);
    }
    if (normalized == "NOTE" || normalized == "TIP" || normalized == "WARNING"
        || normalized == "CAUTION" || normalized == "IMPORTANT") {
        return normalized;
    }
    return std::nullopt;
}

inline CalloutVisualKind callout_visual_kind(std::string_view kind) {
    const auto normalized = normalize_callout_kind(kind);
    if (normalized == "TIP") return CalloutVisualKind::Tip;
    if (normalized == "WARNING" || normalized == "CAUTION") return CalloutVisualKind::Warning;
    return CalloutVisualKind::Note;
}

inline std::u32string callout_display_label(std::string_view kind) {
    const auto normalized = normalize_callout_kind(kind).value_or("NOTE");
    if (normalized == "TIP") return U"Tip";
    if (normalized == "WARNING") return U"Warning";
    if (normalized == "CAUTION") return U"Caution";
    if (normalized == "IMPORTANT") return U"Important";
    return U"Note";
}

inline std::u32string rewrite_callout_opening_marker(
    std::u32string_view marker,
    std::string_view kind) {
    const auto normalized = normalize_callout_kind(kind).value_or("NOTE");
    auto rewritten = std::u32string(marker);
    const auto start = rewritten.find(U"[!");
    if (start == std::u32string::npos) return {};
    const auto end = rewritten.find(U']', start + 2);
    if (end == std::u32string::npos) return {};
    std::u32string replacement;
    replacement.reserve(normalized.size());
    for (const auto ch : normalized) replacement.push_back(static_cast<char32_t>(ch));
    rewritten.replace(start + 2, end - start - 2, replacement);
    return rewritten;
}

} // namespace elmd

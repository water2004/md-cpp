// folia.core.slug — slug generation with collision handling.
export module folia.core.slug;
import std;

export namespace folia {

inline std::string percent_decode_url_component(std::string_view value) {
    const auto hex_value = [](unsigned char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
        if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
        return -1;
    };

    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size();) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const auto high = hex_value(static_cast<unsigned char>(value[index + 1]));
            const auto low = hex_value(static_cast<unsigned char>(value[index + 2]));
            const auto byte = high >= 0 && low >= 0 ? (high << 4) | low : -1;
            // NUL cannot participate in a URI-to-filesystem/heading lookup
            // boundary.  Preserve its spelling instead of silently
            // truncating a later native string conversion.
            if (byte > 0) {
                decoded.push_back(static_cast<char>(byte));
                index += 3;
                continue;
            }
        }
        decoded.push_back(value[index]);
        ++index;
    }
    return decoded;
}

inline std::string to_lower_ascii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Generate a slug from a title. Reproduces the Rust algorithm:
//   lowercase -> keep alnum/-/_ -> '-' otherwise -> trim leading/trailing '-'
//   -> collapse consecutive '-' -> if empty "section" -> if duplicate, "-<N>".
inline std::string generate_slug(const std::string& title,
                                 const std::vector<std::string>& existing) {
    std::string low = to_lower_ascii(title);
    std::string base;
    base.reserve(low.size());
    for (char c : low) {
        const auto byte = static_cast<unsigned char>(c);
        // UTF-8 non-ASCII bytes are retained as one encoded code-point
        // sequence.  The previous ASCII-only port collapsed every CJK
        // heading to `section`, which made its page URL impossible to target.
        if (byte >= 0x80 || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') base.push_back(c);
        else base.push_back('-');
    }
    // trim leading/trailing '-'
    auto a = base.find_first_not_of('-');
    if (a == std::string::npos) base = "section";
    else {
        auto b = base.find_last_not_of('-');
        base = base.substr(a, b - a + 1);
        // collapse consecutive '-'
        std::string collapsed;
        bool prev_dash = false;
        for (char c : base) {
            if (c == '-') { if (prev_dash) continue; prev_dash = true; }
            else prev_dash = false;
            collapsed.push_back(c);
        }
        base = collapsed;
    }
    // collision check
    auto contains = [&](const std::string& s) {
        for (const auto& e : existing) if (e == s) return true;
        return false;
    };
    if (!contains(base)) return base;
    std::size_t n = 1;
    while (true) {
        std::string cand = base + "-" + std::to_string(n);
        if (!contains(cand)) return cand;
        ++n;
    }
}

// Unique slugs for a list of titles, accumulating used-slugs in order.
inline std::vector<std::string> generate_unique_slugs(const std::vector<std::string>& titles) {
    std::vector<std::string> used, out;
    for (const auto& t : titles) { auto s = generate_slug(t, used); out.push_back(s); used.push_back(s); }
    return out;
}

} // namespace folia

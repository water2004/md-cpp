// folia.core.slug — slug generation with collision handling.
export module folia.core.slug;
import std;

export namespace folia {

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
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') base.push_back(c);
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
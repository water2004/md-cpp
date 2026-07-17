// elmd.core.html_inline_presentation — safe HTML inline style projection.
// HTML remains a lossless CST syntax island; this module derives presentation
// only and never mutates or normalizes InlineDocument.source.
export module elmd.core.html_inline_presentation;
import std;
import elmd.core.render_model;
import elmd.core.theme;

export namespace elmd {

namespace html_inline_presentation_detail {

inline std::string_view trim_ascii(std::string_view value) {
    while (!value.empty() && static_cast<unsigned char>(value.front()) <= 0x20) value.remove_prefix(1);
    while (!value.empty() && static_cast<unsigned char>(value.back()) <= 0x20) value.remove_suffix(1);
    return value;
}

inline std::string lower_ascii(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

inline std::optional<std::uint8_t> hex_byte(char high, char low) {
    auto digit = [](char value) -> std::optional<std::uint8_t> {
        if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(10 + value - 'a');
        if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(10 + value - 'A');
        return std::nullopt;
    };
    const auto a = digit(high);
    const auto b = digit(low);
    if (!a || !b) return std::nullopt;
    return static_cast<std::uint8_t>((*a << 4) | *b);
}

inline std::optional<Color> parse_hex_color(std::string_view value) {
    if (value.empty() || value.front() != '#') return std::nullopt;
    value.remove_prefix(1);
    if (value.size() == 3 || value.size() == 4) {
        std::array<std::uint8_t, 4> parts{0, 0, 0, 255};
        for (std::size_t index = 0; index < value.size(); ++index) {
            const auto component = hex_byte(value[index], value[index]);
            if (!component) return std::nullopt;
            parts[index] = *component;
        }
        return Color(parts[0], parts[1], parts[2], parts[3]);
    }
    if (value.size() == 6 || value.size() == 8) {
        std::array<std::uint8_t, 4> parts{0, 0, 0, 255};
        for (std::size_t index = 0; index < value.size() / 2; ++index) {
            const auto component = hex_byte(value[index * 2], value[index * 2 + 1]);
            if (!component) return std::nullopt;
            parts[index] = *component;
        }
        return Color(parts[0], parts[1], parts[2], parts[3]);
    }
    return std::nullopt;
}

inline std::optional<float> parse_float(std::string_view value) {
    value = trim_ascii(value);
    if (value.empty()) return std::nullopt;
    float result = 0.0f;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
        || !std::isfinite(result)) return std::nullopt;
    return result;
}

inline std::optional<Color> parse_function_color(std::string_view value) {
    const auto lower = lower_ascii(value);
    const bool rgba = lower.starts_with("rgba(");
    if (!rgba && !lower.starts_with("rgb(")) return std::nullopt;
    if (lower.back() != ')') return std::nullopt;
    auto body = std::string_view(lower).substr(rgba ? 5 : 4);
    body.remove_suffix(1);
    std::array<std::string_view, 4> components{};
    std::size_t count = 0;
    while (!body.empty() && count < components.size()) {
        const auto comma = body.find(',');
        components[count++] = trim_ascii(body.substr(0, comma));
        if (comma == std::string_view::npos) {
            body = {};
        } else {
            body.remove_prefix(comma + 1);
        }
    }
    if (!body.empty() || count != (rgba ? 4u : 3u)) return std::nullopt;
    std::array<std::uint8_t, 4> result{0, 0, 0, 255};
    for (std::size_t index = 0; index < 3; ++index) {
        auto component = components[index];
        const bool percent = !component.empty() && component.back() == '%';
        if (percent) component.remove_suffix(1);
        const auto number = parse_float(component);
        if (!number) return std::nullopt;
        const auto scaled = percent ? *number * 2.55f : *number;
        if (scaled < 0.0f || scaled > 255.0f) return std::nullopt;
        result[index] = static_cast<std::uint8_t>(std::lround(scaled));
    }
    if (rgba) {
        auto component = components[3];
        const bool percent = !component.empty() && component.back() == '%';
        if (percent) component.remove_suffix(1);
        const auto number = parse_float(component);
        if (!number) return std::nullopt;
        const auto scaled = percent ? *number * 2.55f : *number * 255.0f;
        if (scaled < 0.0f || scaled > 255.0f) return std::nullopt;
        result[3] = static_cast<std::uint8_t>(std::lround(scaled));
    }
    return Color(result[0], result[1], result[2], result[3]);
}

inline std::optional<Color> parse_color(std::string_view value) {
    value = trim_ascii(value);
    if (const auto parsed = parse_hex_color(value)) return parsed;
    if (const auto parsed = parse_function_color(value)) return parsed;
    static const std::unordered_map<std::string_view, Color> named{
        {"transparent", Color(0, 0, 0, 0)}, {"black", Color(0, 0, 0)},
        {"white", Color(255, 255, 255)}, {"red", Color(255, 0, 0)},
        {"green", Color(0, 128, 0)}, {"blue", Color(0, 0, 255)},
        {"yellow", Color(255, 255, 0)}, {"gray", Color(128, 128, 128)},
        {"grey", Color(128, 128, 128)}, {"orange", Color(255, 165, 0)},
        {"purple", Color(128, 0, 128)}, {"magenta", Color(255, 0, 255)},
        {"fuchsia", Color(255, 0, 255)}, {"cyan", Color(0, 255, 255)},
        {"aqua", Color(0, 255, 255)}, {"navy", Color(0, 0, 128)},
        {"teal", Color(0, 128, 128)}, {"maroon", Color(128, 0, 0)},
        {"olive", Color(128, 128, 0)}, {"silver", Color(192, 192, 192)},
    };
    const auto lower = lower_ascii(value);
    if (const auto found = named.find(lower); found != named.end()) return found->second;
    return std::nullopt;
}

inline std::optional<std::pair<float, std::string>> number_and_unit(std::string_view value) {
    value = trim_ascii(value);
    if (value.empty()) return std::nullopt;
    std::size_t number_end = 0;
    while (number_end < value.size()) {
        const auto ch = value[number_end];
        if ((ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.'
            || ch == 'e' || ch == 'E') {
            ++number_end;
        } else {
            break;
        }
    }
    const auto number = parse_float(value.substr(0, number_end));
    if (!number) return std::nullopt;
    return std::pair{*number, lower_ascii(trim_ascii(value.substr(number_end)))};
}

inline std::optional<std::string> safe_font_family(std::string_view value) {
    value = trim_ascii(value);
    if (const auto comma = value.find(','); comma != std::string_view::npos) value = value.substr(0, comma);
    value = trim_ascii(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
        || (value.front() == '\'' && value.back() == '\''))) {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    value = trim_ascii(value);
    if (value.empty() || value.size() > 128) return std::nullopt;
    if (std::ranges::any_of(value, [](unsigned char ch) {
            return ch < 0x20 || ch == '<' || ch == '>' || ch == ';';
        })) return std::nullopt;
    return std::string(value);
}

inline std::pair<std::string_view, std::string_view> take_declaration(
    std::string_view declarations) {
    char quote = 0;
    std::size_t parentheses = 0;
    bool escaped = false;
    for (std::size_t index = 0; index < declarations.size(); ++index) {
        const auto ch = declarations[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && quote != 0) {
            escaped = true;
            continue;
        }
        if (quote != 0) {
            if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '(') {
            ++parentheses;
        } else if (ch == ')' && parentheses > 0) {
            --parentheses;
        } else if (ch == ';' && parentheses == 0) {
            return {declarations.substr(0, index), declarations.substr(index + 1)};
        }
    }
    return {declarations, {}};
}

inline void scale_font(InlinePresentationStyle& style, float scale) {
    scale = std::clamp(scale, 0.25f, 4.0f);
    if (style.absolute_font_size) {
        style.absolute_font_size = std::clamp(*style.absolute_font_size * scale, 6.0f, 144.0f);
    } else {
        style.relative_font_scale = std::clamp(style.relative_font_scale * scale, 0.25f, 4.0f);
    }
}

} // namespace html_inline_presentation_detail

inline InlineStyle apply_html_inline_presentation(
    InlineStyle inherited,
    std::string_view tag,
    const std::unordered_map<std::string, std::string>& attributes) {
    using namespace html_inline_presentation_detail;
    std::optional<InlinePresentationStyle> presentation;
    auto ensure_presentation = [&]() -> InlinePresentationStyle& {
        if (!presentation) {
            presentation = inherited.presentation
                ? *inherited.presentation
                : InlinePresentationStyle{};
        }
        return *presentation;
    };

    if (tag == "strong" || tag == "b") inherited.bold = true;
    else if (tag == "em" || tag == "i" || tag == "cite" || tag == "var") inherited.italic = true;
    else if (tag == "del" || tag == "s" || tag == "strike") inherited.strikethrough = true;
    else if (tag == "u" || tag == "ins") inherited.underline = true;
    else if (tag == "code" || tag == "kbd" || tag == "samp" || tag == "tt") inherited.code = true;
    else if (tag == "small") scale_font(ensure_presentation(), 0.83f);
    else if (tag == "sub") {
        auto& style = ensure_presentation();
        style.baseline = InlineBaseline::Subscript;
        scale_font(style, 0.83f);
    } else if (tag == "sup") {
        auto& style = ensure_presentation();
        style.baseline = InlineBaseline::Superscript;
        scale_font(style, 0.83f);
    } else if (tag == "mark") {
        ensure_presentation().highlight = true;
    }

    const auto found_style = attributes.find("style");
    if (found_style != attributes.end() && found_style->second.size() <= 4096) {
        auto declarations = std::string_view(found_style->second);
        while (!declarations.empty()) {
            auto [raw_declaration, remaining] = take_declaration(declarations);
            auto declaration = trim_ascii(raw_declaration);
            declarations = remaining;
            const auto colon = declaration.find(':');
            if (colon == std::string_view::npos) continue;
            const auto property = lower_ascii(trim_ascii(declaration.substr(0, colon)));
            const auto value = trim_ascii(declaration.substr(colon + 1));
            const auto lower_value = lower_ascii(value);
            if (lower_value.find("url(") != std::string::npos
                || lower_value.find("expression(") != std::string::npos
                || lower_value.find("var(") != std::string::npos) continue;

            if (property == "color") {
                if (const auto color = parse_color(value)) ensure_presentation().foreground = color;
            } else if (property == "background-color" || property == "background") {
                if (const auto color = parse_color(value)) ensure_presentation().background = color;
            } else if (property == "font-weight") {
                if (lower_value == "normal") {
                    inherited.bold = false;
                    ensure_presentation().font_weight = 400;
                } else if (lower_value == "bold" || lower_value == "bolder") {
                    inherited.bold = true;
                    ensure_presentation().font_weight = 700;
                } else if (lower_value == "lighter") {
                    inherited.bold = false;
                    ensure_presentation().font_weight = 300;
                } else {
                    std::uint16_t weight = 0;
                    const auto parsed = std::from_chars(
                        lower_value.data(), lower_value.data() + lower_value.size(), weight);
                    if (parsed.ec == std::errc{} && parsed.ptr == lower_value.data() + lower_value.size()
                        && weight >= 100 && weight <= 900) {
                        inherited.bold = weight >= 600;
                        ensure_presentation().font_weight = weight;
                    }
                }
            } else if (property == "font-style") {
                if (lower_value == "normal") inherited.italic = false;
                else if (lower_value == "italic" || lower_value == "oblique") inherited.italic = true;
            } else if (property == "text-decoration" || property == "text-decoration-line") {
                if (lower_value == "none") {
                    inherited.underline = false;
                    inherited.strikethrough = false;
                } else {
                    if (lower_value.find("underline") != std::string::npos) inherited.underline = true;
                    if (lower_value.find("line-through") != std::string::npos) inherited.strikethrough = true;
                }
            } else if (property == "font-family") {
                if (const auto family = safe_font_family(value)) ensure_presentation().font_family = *family;
            } else if (property == "font-size") {
                if (lower_value == "smaller") {
                    scale_font(ensure_presentation(), 0.83f);
                } else if (lower_value == "larger") {
                    scale_font(ensure_presentation(), 1.2f);
                } else if (const auto parsed = number_and_unit(lower_value)) {
                    const auto [number, unit] = *parsed;
                    if (unit == "px" || unit == "pt") {
                        auto& style = ensure_presentation();
                        style.absolute_font_size = std::clamp(
                            unit == "pt" ? number * (4.0f / 3.0f) : number,
                            6.0f,
                            144.0f);
                        style.relative_font_scale = 1.0f;
                    } else if (unit == "em" || unit == "rem") {
                        scale_font(ensure_presentation(), number);
                    } else if (unit == "%") {
                        scale_font(ensure_presentation(), number / 100.0f);
                    }
                }
            } else if (property == "vertical-align") {
                auto& style = ensure_presentation();
                if (lower_value == "sub") style.baseline = InlineBaseline::Subscript;
                else if (lower_value == "super") style.baseline = InlineBaseline::Superscript;
                else if (lower_value == "baseline") style.baseline = InlineBaseline::Normal;
            }
        }
    }

    if (presentation) inherited.presentation = std::make_shared<const InlinePresentationStyle>(std::move(*presentation));
    return inherited;
}

} // namespace elmd

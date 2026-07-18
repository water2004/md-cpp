// folia.core.diagnostics — diagnostic types.
export module folia.core.diagnostics;
import std;
import folia.core.text_edit;

export namespace folia {

enum class DiagnosticSeverity { Hint, Warning, Error };

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    std::string message;
    std::optional<TextSpan> source_span;
    std::optional<std::string> code;
};

// Diagnostic code constants (match Rust codes).
inline constexpr const char* DIAG_UNCLOSED_MATH_DOLLAR       = "E001";
inline constexpr const char* DIAG_UNCLOSED_CODE_FENCE        = "E002";
inline constexpr const char* DIAG_TABLE_SEPARATOR_MISMATCH   = "E003";
inline constexpr const char* DIAG_IMAGE_PATH_MISSING         = "E004";
inline constexpr const char* DIAG_UNSAFE_HTML                = "E005";
inline constexpr const char* DIAG_MATH_RENDER_FALLBACK       = "E006";
inline constexpr const char* DIAG_DUPLICATE_FOOTNOTE        = "E007";
inline constexpr const char* DIAG_TOC_DISABLED              = "E008";
inline constexpr const char* DIAG_UNKNOWN_EXTENSION         = "E009";
inline constexpr const char* DIAG_MALFORMED_SYNTAX         = "E010";
inline constexpr const char* DIAG_MISSING_ALT_TEXT           = "W001";
inline constexpr const char* DIAG_HEADING_NO_SPACE          = "W002";

inline Diagnostic make_diagnostic(DiagnosticSeverity sev, std::string msg,
                                  std::optional<TextSpan> span = std::nullopt,
                                  std::optional<std::string> code = std::nullopt) {
    Diagnostic d; d.severity = sev; d.message = std::move(msg);
    d.source_span = span; d.code = std::move(code); return d;
}

} // namespace folia

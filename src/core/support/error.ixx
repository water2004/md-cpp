// elmd.core.error — error model.
export module elmd.core.error;
import std;

export namespace elmd {

enum class ErrorCode {
    Io, Parse, Render, Layout, Export, InvalidOffset, InvalidRange,
    RevisionMismatch, MathRender, Unsupported, Clipboard, Platform, Other,
};

class EditorError {
public:
    ErrorCode code;
    std::string message;
    std::uint64_t expected{};
    std::uint64_t actual{};

    EditorError(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
    static EditorError revision_mismatch(std::uint64_t exp, std::uint64_t act) {
        EditorError e(ErrorCode::RevisionMismatch, "revision mismatch");
        e.expected = exp; e.actual = act; return e;
    }
};

template <typename T>
class EditorResult {
public:
    bool ok = true;
    T value{};
    EditorError error{ErrorCode::Other, ""};

    EditorResult() = default;
    EditorResult(T v) : ok(true), value(std::move(v)) {}
    static EditorResult fail(EditorError e) { EditorResult r; r.ok = false; r.error = std::move(e); return r; }
    explicit operator bool() const { return ok; }
};

template <>
class EditorResult<void> {
public:
    bool ok = true;
    EditorError error{ErrorCode::Other, ""};

    EditorResult() = default;
    static EditorResult fail(EditorError e) { EditorResult r; r.ok = false; r.error = std::move(e); return r; }
    explicit operator bool() const { return ok; }
};

} // namespace elmd
// elmd.core.storage — portable file IO + asset manager.
// Pure core, portable. WinUI 3 / Windows file dialog integration belongs in
// the app layer; this module only owns raw path/byte IO and asset path policy,
// mirroring the Rust storage crate.
export module elmd.core.storage;
import std;
import elmd.core.error;

export namespace elmd {

namespace fs = std::filesystem;

inline EditorResult<std::string> read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return EditorResult<std::string>::fail(EditorError(ErrorCode::Io, "open failed"));
    std::stringstream ss; ss << f.rdbuf();
    EditorResult<std::string> r(ss.str());
    return r;
}

inline EditorResult<void> write_file(const fs::path& path, std::string_view content) {
    std::error_code ec;
    if (auto p = path.parent_path(); !p.empty()) fs::create_directories(p, ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) return EditorResult<void>::fail(EditorError(ErrorCode::Io, "write failed"));
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return {};
}

inline bool file_exists(const fs::path& p) { return fs::exists(p) && fs::is_regular_file(p); }

class AssetManager {
public:
    AssetManager() = default;
    explicit AssetManager(fs::path assets_dir) : assets_dir_(std::move(assets_dir)) {}
    void set_assets_dir(fs::path d) { assets_dir_ = std::move(d); }
    const std::optional<fs::path>& assets_dir() const { return assets_dir_; }

    fs::path resolve_asset_path(const fs::path& doc_path, std::string_view src) const {
        std::string s(src);
        if (s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0) return fs::path(s);
        if (assets_dir_) return *assets_dir_ / fs::path(s).filename();
        fs::path base = doc_path.parent_path().empty() ? fs::path(".") : doc_path.parent_path();
        return base / fs::path(s);
    }

    EditorResult<fs::path> copy_asset(const fs::path& source, const fs::path& doc_path, std::string_view filename) const {
        fs::path dest_dir = assets_dir_ ? *assets_dir_ : doc_path.parent_path() / "assets";
        std::error_code ec;
        fs::create_directories(dest_dir, ec);
        fs::path dest = dest_dir / fs::path(filename);
        fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) return EditorResult<fs::path>::fail(EditorError(ErrorCode::Io, "copy failed"));
        return EditorResult<fs::path>(dest);
    }

private:
    std::optional<fs::path> assets_dir_;
};

} // namespace elmd
// elmd.core.scheduler — parse/render scheduler (revision tracking).
// Pure core. UI owns real threading; this is the policy object the UI layer
// queries to decide whether to kick a parse and record its completion.
export module elmd.core.scheduler;
import std;

export namespace elmd {

class ParseScheduler {
public:
    std::uint64_t debounce_ms = 50;
    bool running = false;
    std::uint64_t last_revision = 0;

    ParseScheduler() = default;
    explicit ParseScheduler(std::uint64_t debounce) : debounce_ms(debounce) {}

    bool should_parse(std::uint64_t current_revision) const {
        return !running && current_revision > last_revision;
    }
    void mark_parsing() { running = true; }
    void complete_parse(std::uint64_t revision) { running = false; last_revision = revision; }
};

} // namespace elmd
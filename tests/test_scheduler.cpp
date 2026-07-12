import std;
import boost.ut;
import elmd.core.scheduler;

using namespace elmd;
using namespace boost::ut;


suite scheduler_tests = [] {

"test_scheduler_initial_state"_test = [] {
    ParseScheduler s;
    expect(fatal(bool((s.debounce_ms) == (50ull))));
    expect(fatal(bool(s.last_revision == 0ull)));
    expect(fatal(bool(s.running == false)));
};

"test_scheduler_should_parse"_test = [] {
    ParseScheduler s;
    expect(fatal(bool(s.should_parse(1ull) == true)));
    s.mark_parsing();
    expect(fatal(bool(s.should_parse(1ull) == false)));
    s.complete_parse(1ull);
    expect(fatal(bool(s.should_parse(1ull) == false)));
    expect(fatal(bool(s.should_parse(2ull) == true)));
};

"test_scheduler_should_parse_idle_when_current_lower"_test = [] {
    ParseScheduler s;
    s.complete_parse(5ull);
    expect(fatal(bool(s.should_parse(3ull) == false)));
};

}; // suite scheduler_tests

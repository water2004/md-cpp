import std;
#include "test_framework.h"
import elmd.core.scheduler;

using namespace elmd;

ELMD_TEST(test_scheduler_initial_state) {
    ParseScheduler s;
    ELMD_CHECK_EQ(s.debounce_ms, 50ull);
    ELMD_CHECK(s.last_revision == 0ull);
    ELMD_CHECK(s.running == false);
}

ELMD_TEST(test_scheduler_should_parse) {
    ParseScheduler s;
    ELMD_CHECK(s.should_parse(1ull) == true);
    s.mark_parsing();
    ELMD_CHECK(s.should_parse(1ull) == false);
    s.complete_parse(1ull);
    ELMD_CHECK(s.should_parse(1ull) == false);
    ELMD_CHECK(s.should_parse(2ull) == true);
}

ELMD_TEST(test_scheduler_should_parse_idle_when_current_lower) {
    ParseScheduler s;
    s.complete_parse(5ull);
    ELMD_CHECK(s.should_parse(3ull) == false);
}
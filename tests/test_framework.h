// el-md test framework — header-only so ELMD_* macros are visible to all
// translation units via textual inclusion (macros cannot cross modules).
//
// IMPORTANT: this header deliberately includes NO standard-library headers.
// It uses std::vector / std::string / std::printf, which must already be
// visible in the including TU via a preceding `import std;`. The test TUs do:
//
//     import std;
//     #include "test_framework.h"
//     import elmd.core.*;
//
// Keeping a single std source (`import std;`, matching the elmd.core modules'
// own `import std;`) avoids both:
//   - C2011 duplicate-definition errors (no textual `#include <vector>` etc.
//     colliding with `import std;`), and
//   - LNK2019 ABI mismatches that occur when header-introduced std types
//     differ from module-BMI std types (inline functions crossing the
//     module boundary, e.g. FallbackMathRenderer::render_inline(const
//     std::string&), would otherwise reference a different std::string).
//
// Including this header without a prior `import std;` is an error.
#ifndef ELMD_TEST_FRAMEWORK_H
#define ELMD_TEST_FRAMEWORK_H

namespace elmdtest {

inline int& fail_count() { static int s = 0; return s; }
inline int& test_count() { static int s = 0; return s; }

struct TestCase { void(*run)(); const char* name; };
inline std::vector<TestCase>& tests() { static std::vector<TestCase> v; return v; }
inline void register_test(void(*f)(), const char* name) { tests().push_back(TestCase{f, name}); }

inline void record_fail(const char* file, int line, std::string msg) {
    ++fail_count();
    std::printf("FAIL %s:%d %s\n", file, line, msg.c_str());
}

inline int run_all() {
    auto& v = tests();
#pragma warning(push)
#pragma warning(disable: 4996)
    const auto* raw_filter = std::getenv("ELMD_TEST_FILTER");
#pragma warning(pop)
    const auto filter = raw_filter ? std::string_view(raw_filter) : std::string_view{};
    for (const auto& test : v) {
        if (!filter.empty() && !std::string_view(test.name).contains(filter)) continue;
        ++test_count();
        test.run();
    }
    if (fail_count() == 0) {
        std::printf("ALL %d TESTS PASSED\n", test_count());
        return 0;
    }
    std::printf("%d/%d TESTS FAILED\n", fail_count(), static_cast<int>(v.size()));
    return 1;
}

} // namespace elmdtest

inline int elmd_tests_main() { return ::elmdtest::run_all(); }

#define ELMD_TEST(name) \
    static void name(); \
    struct name##_reg { name##_reg() { ::elmdtest::register_test(&name, #name); } }; \
    static name##_reg name##_reg_; \
    static void name()

#define ELMD_CHECK(cond) \
    do { if (!bool(cond)) { ::elmdtest::record_fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); } } while (0)

#define ELMD_CHECK_EQ(a, b) \
    do { if (!((a) == (b))) { ::elmdtest::record_fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ") failed"); } } while (0)

#endif // ELMD_TEST_FRAMEWORK_H

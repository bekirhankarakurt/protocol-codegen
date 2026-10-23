#pragma once

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*run)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

inline void report_failure(const char* file, int line, const std::string& message) {
    std::fprintf(stderr, "  %s:%d: %s\n", file, line, message.c_str());
    ++failure_count();
}

/// Runs every registered case; returns the process exit code.
inline int run_all() {
    int failed_cases = 0;
    for (const Case& c : registry()) {
        const int before = failure_count();
        try {
            c.run();
        } catch (const std::exception& e) {
            report_failure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        const bool ok = failure_count() == before;
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", c.name);
        if (!ok) ++failed_cases;
    }
    std::printf("%zu cases, %d failed\n", registry().size(), failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace test

#define TEST(name)                                                  \
    static void name();                                             \
    static const test::Registrar registrar_##name(#name, &name);    \
    static void name()

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) test::report_failure(__FILE__, __LINE__, "CHECK failed: " #expr); \
    } while (false)

#define CHECK_EQ(actual, expected)                                                         \
    do {                                                                                   \
        if (!((actual) == (expected))) {                                                   \
            test::report_failure(__FILE__, __LINE__, "CHECK_EQ failed: " #actual " == " #expected); \
        }                                                                                  \
    } while (false)

/// Passes when `expr` throws an exception whose message contains `fragment`.
#define CHECK_THROWS_WITH(expr, fragment)                                                 \
    do {                                                                                  \
        std::string message_;                                                             \
        bool threw_ = false;                                                              \
        try {                                                                             \
            (void)(expr);                                                                 \
        } catch (const std::exception& e_) {                                              \
            threw_ = true;                                                                \
            message_ = e_.what();                                                         \
        }                                                                                 \
        if (!threw_) {                                                                    \
            test::report_failure(__FILE__, __LINE__, "no exception from: " #expr);        \
        } else if (message_.find(fragment) == std::string::npos) {                        \
            test::report_failure(__FILE__, __LINE__,                                      \
                                 "message \"" + message_ + "\" lacks \"" fragment "\"");   \
        }                                                                                 \
    } while (false)

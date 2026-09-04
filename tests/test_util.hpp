#pragma once

#include <cstdio>
#include <sstream>
#include <string>

namespace cf_test {

inline int failures = 0;

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& detail = "") {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s:%d: %s %s\n", file, line, expr, detail.c_str());
    }
}

}  // namespace cf_test

#define CHECK(expr) ::cf_test::report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_M(expr, msg) ::cf_test::report(static_cast<bool>(expr), #expr, __FILE__, __LINE__, (msg))
#define CF_TEST_RETURN() (::cf_test::failures == 0 ? 0 : 1)
#define CF_TEST_SUMMARY() std::printf("%d failure(s)\n", ::cf_test::failures)

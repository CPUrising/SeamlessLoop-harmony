/*
 * 极简单测框架：零依赖，配合 ctest 使用。
 *
 * 设计取舍：本变更只验证播放内核，不引入 Catch2 / GoogleTest 之类的新依赖，
 * 避免为测试引入额外构建复杂度。断言失败即打印并计数，进程退出码非 0 表示失败。
 */

#ifndef SEAMLESS_LOOP_TEST_MAIN_H
#define SEAMLESS_LOOP_TEST_MAIN_H

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace slh_test {

inline int& FailureCount()
{
    static int count = 0;
    return count;
}

inline int& AssertionCount()
{
    static int count = 0;
    return count;
}

inline const char*& CurrentTest()
{
    static const char* name = "";
    return name;
}

inline void ReportFailure(const char* file, int line, const char* expr)
{
    ++FailureCount();
    std::printf("  [FAIL] %s:%d  %s  (test: %s)\n", file, line, expr, CurrentTest());
}

} // namespace slh_test

#define TEST_CASE(name)                                              \
    static void name();                                              \
    static void name##_runner()                                      \
    {                                                                \
        slh_test::CurrentTest() = #name;                             \
        const int before = slh_test::FailureCount();                 \
        name();                                                      \
        if (slh_test::FailureCount() == before) {                    \
            std::printf("  [PASS] %s\n", #name);                     \
        }                                                            \
    }                                                                \
    static void name()

#define CHECK_TRUE(expr)                                             \
    do {                                                             \
        ++slh_test::AssertionCount();                                \
        if (!(expr)) {                                               \
            slh_test::ReportFailure(__FILE__, __LINE__, #expr);      \
        }                                                            \
    } while (false)

#define CHECK_FALSE(expr) CHECK_TRUE(!(expr))

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        ++slh_test::AssertionCount();                                          \
        const auto _a = (actual);                                              \
        const auto _e = (expected);                                            \
        if (!(_a == _e)) {                                                     \
            char _buf[256];                                                    \
            std::snprintf(_buf, sizeof(_buf), "%s == %s (got %lld, want %lld)", \
                          #actual, #expected,                                      \
                          static_cast<long long>(_a),                              \
                          static_cast<long long>(_e));                             \
            slh_test::ReportFailure(__FILE__, __LINE__, _buf);                  \
        }                                                                      \
    } while (false)

#define CHECK_NEAR(actual, expected, tol)                                      \
    do {                                                                       \
        ++slh_test::AssertionCount();                                           \
        const double _a = static_cast<double>(actual);                          \
        const double _e = static_cast<double>(expected);                        \
        if (std::fabs(_a - _e) > (tol)) {                                       \
            char _buf[256];                                                     \
            std::snprintf(_buf, sizeof(_buf), "%s ~= %s (got %f, want %f)",      \
                          #actual, #expected, _a, _e);                          \
            slh_test::ReportFailure(__FILE__, __LINE__, _buf);                  \
        }                                                                       \
    } while (false)

// 在 main 中调用，汇总并返回进程退出码。
inline int Summarize(const char* suiteName)
{
    std::printf("\n%s: %d assertions, %d failure(s)\n",
                suiteName, slh_test::AssertionCount(), slh_test::FailureCount());
    return slh_test::FailureCount() == 0 ? 0 : 1;
}

#endif // SEAMLESS_LOOP_TEST_MAIN_H

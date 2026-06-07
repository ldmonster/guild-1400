#pragma once
// Minimal dependency-free unit-test framework. Define tests with TEST(suite, name)
// and assert with CHECK / CHECK_EQ. A single test_main.cpp provides main().
#include <cstdio>
#include <vector>

namespace gtl {

struct TestCase { const char* suite; const char* name; void (*fn)(); };

std::vector<TestCase>& Registry();
int&  Failures();
long& Checks();
int   RunAll();

struct Registrar { Registrar(const char* suite, const char* name, void (*fn)()); };

} // namespace gtl

#define TEST(suite, name)                                                       \
    static void suite##_##name##_fn();                                          \
    static ::gtl::Registrar suite##_##name##_reg(#suite, #name,                 \
                                                 &suite##_##name##_fn);         \
    static void suite##_##name##_fn()

#define CHECK(cond)                                                             \
    do {                                                                        \
        ::gtl::Checks()++;                                                      \
        if (!(cond)) {                                                          \
            ::gtl::Failures()++;                                                \
            std::printf("    FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__,      \
                        #cond);                                                 \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        ::gtl::Checks()++;                                                      \
        auto _va = (a);                                                         \
        auto _vb = (b);                                                         \
        if (!(_va == _vb)) {                                                    \
            ::gtl::Failures()++;                                                \
            std::printf("    FAIL %s:%d: CHECK_EQ(%s, %s)\n", __FILE__,         \
                        __LINE__, #a, #b);                                      \
        }                                                                       \
    } while (0)

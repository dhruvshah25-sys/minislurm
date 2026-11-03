#pragma once
// A 40-line test framework. No dependencies, nothing magic: each TEST()
// registers a function in a global list at static-init time, and main()
// runs them. Worth reading once: it's how GoogleTest works underneath.
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace msltest {

struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }

struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};

}  // namespace msltest

#define TEST(name)                                                        \
    static void name();                                                   \
    static msltest::Registrar name##_reg{#name, name};                    \
    static void name()

#define CHECK(expr)                                                       \
    do {                                                                  \
        if (!(expr)) {                                                    \
            std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, \
                         __LINE__, #expr);                                \
            ++msltest::failures();                                        \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        const auto va = (a);                                              \
        const auto vb = (b);                                              \
        if (!(va == vb)) {                                                \
            std::fprintf(stderr, "  %s:%d: %s == %s failed (%.10g vs %.10g)\n", \
                         __FILE__, __LINE__, #a, #b, static_cast<double>(va), \
                         static_cast<double>(vb));                        \
            ++msltest::failures();                                        \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                             \
    do {                                                                  \
        const double va = (a), vb = (b);                                  \
        if (!(std::fabs(va - vb) <= (eps))) {                             \
            std::fprintf(stderr, "  %s:%d: %s ~= %s failed (%.10g vs %.10g)\n", \
                         __FILE__, __LINE__, #a, #b, va, vb);             \
            ++msltest::failures();                                        \
        }                                                                 \
    } while (0)

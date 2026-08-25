#pragma once

// Minimal test harness — no external dependency, consistent with the rest
// of the prototype. A TEST_CASE registers itself at static-init time;
// test_main.cpp runs every registered case and reports pass/fail.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace testing {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

inline int& failure_count() {
    static int n = 0;
    return n;
}

inline std::string& current_name() {
    static std::string n;
    return n;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline void fail(const char* expr, const char* file, int line) {
    std::cerr << "  [FAIL] " << current_name() << ": " << expr
               << " (" << file << ":" << line << ")\n";
    ++failure_count();
}

} // namespace testing

#define TEST_CASE(name)                                                     \
    static void test_##name();                                             \
    static testing::Registrar registrar_##name(#name, test_##name);        \
    static void test_##name()

#define EXPECT_TRUE(cond)                                                   \
    do {                                                                    \
        if (!(cond)) testing::fail(#cond, __FILE__, __LINE__);              \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                                     \
    do {                                                                    \
        if (!((a) == (b))) testing::fail(#a " == " #b, __FILE__, __LINE__); \
    } while (0)

/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Minimal test helpers — no dependency, one CTest entry per executable
*/

#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace check {

inline int &failures()
{
    static int count = 0;
    return count;
}

inline std::vector<std::pair<std::string, std::function<void()>>> &registry()
{
    static std::vector<std::pair<std::string, std::function<void()>>> tests;
    return tests;
}

struct Register {
    Register(const char *name, std::function<void()> body)
    {
        registry().emplace_back(name, std::move(body));
    }
};

/* Runs every registered test, returns the process exit code. */
inline int run_all()
{
    for (const auto &[name, body] : registry()) {
        const int before = failures();
        body();
        std::cout << (failures() == before ? "[PASS] " : "[FAIL] ") << name << std::endl;
    }
    std::cout << "\n" << registry().size() << " tests, " << failures() << " failed checks"
              << std::endl;
    return failures() == 0 ? 0 : 1;
}

} // namespace check

#define TEST_CASE(name)                                                   \
    static void name();                                                   \
    static const check::Register name##_registration(#name, name);        \
    static void name()

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            std::cerr << __FILE__ << ":" << __LINE__                      \
                      << ": CHECK failed: " #condition << std::endl;      \
            ++check::failures();                                          \
        }                                                                 \
    } while (0)

#define REQUIRE(condition)                                                \
    do {                                                                  \
        if (!(condition)) {                                               \
            CHECK(condition);                                             \
            return;                                                       \
        }                                                                 \
    } while (0)

#include "test_runner.hpp"

#include <iostream>

int main() {
    int total = 0;
    for (auto& c : testing::registry()) {
        testing::current_name() = c.name;
        std::cout << "[ RUN  ] " << c.name << "\n";
        c.fn();
        ++total;
    }

    int failures = testing::failure_count();
    std::cout << "\n" << total << " test(s), " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}

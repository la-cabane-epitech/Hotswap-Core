// main.cpp
#include "SandboxRunner.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0]
                  << " <module_name> <candidate_path> <active_path> [timeout_ms]\n";
        return 1;
    }

    const std::string module_name = argv[1];
    const std::string candidate_path = argv[2];
    const std::string active_path = argv[3];
    const int timeout_ms = (argc >= 5) ? std::atoi(argv[4]) : 2000;

    bool ok = validate_candidate(module_name, candidate_path, active_path, timeout_ms);
    return ok ? 0 : 1;
}

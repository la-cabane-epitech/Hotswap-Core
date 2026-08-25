#include "test_runner.hpp"
#include "../SandboxRunner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

// These are integration-flavoured unit tests: validate_candidate() isolates
// untrusted code via fork()+dlopen(), which can't be exercised without a
// real .so on disk. Each case compiles a tiny throwaway plugin, runs it
// through the sandbox, and checks the verdict + that active_path is only
// ever touched on a pass.

namespace {

bool file_exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}

void remove_all(std::initializer_list<std::string> paths) {
    for (const auto& p : paths) std::remove(p.c_str());
}

// Compiles `source` (a full .cpp, #include "plugin.hpp" resolved via -I..)
// into a .so at so_path. Returns true on success.
bool build_plugin(const std::string& source, const std::string& so_path) {
    std::string src_path = so_path + ".cpp";
    write_file(src_path, source);
    std::string cmd = "g++ -std=c++17 -I.. -shared -fPIC -o " + so_path + " " + src_path + " 2>/dev/null";
    return system(cmd.c_str()) == 0;
}

const char* GOOD_PLUGIN = R"CPP(
#include "plugin.hpp"
void plugin_update(State* state) { state->counter++; }
)CPP";

const char* SEGFAULT_PLUGIN = R"CPP(
#include "plugin.hpp"
void plugin_update(State*) { int* p = nullptr; *p = 1; }
)CPP";

const char* INFINITE_LOOP_PLUGIN = R"CPP(
#include "plugin.hpp"
void plugin_update(State*) { while (true) {} }
)CPP";

const char* THROWING_PLUGIN = R"CPP(
#include "plugin.hpp"
#include <stdexcept>
void plugin_update(State*) { throw std::runtime_error("boom"); }
)CPP";

const char* MISSING_SYMBOL_PLUGIN = R"CPP(
// deliberately does NOT define plugin_update
int unrelated_symbol = 42;
)CPP";

} // namespace

TEST_CASE(good_candidate_is_promoted_to_active) {
    build_plugin(GOOD_PLUGIN, "cand_good.so");
    write_file("active_good.so", "OLD-VERSION");

    bool ok = validate_candidate("t_good", "./cand_good.so", "./active_good.so", 2000);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(file_exists("./cand_good.so")); // consumed by the rename on promotion
    EXPECT_TRUE(file_exists("./active_good.so"));
    EXPECT_TRUE(read_file("t_good.status.json").find("\"state\": \"sandbox_passed\"") != std::string::npos);

    remove_all({"active_good.so", "t_good.status.json", "t_good.log", "cand_good.so.cpp"});
}

TEST_CASE(segfaulting_candidate_is_rejected_and_active_untouched) {
    build_plugin(SEGFAULT_PLUGIN, "cand_segv.so");
    write_file("active_segv.so", "KNOWN-GOOD");

    bool ok = validate_candidate("t_segv", "./cand_segv.so", "./active_segv.so", 2000);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(file_exists("./cand_segv.so")); // left in place for post-mortem
    EXPECT_EQ(read_file("./active_segv.so"), std::string("KNOWN-GOOD"));
    std::string status = read_file("t_segv.status.json");
    EXPECT_TRUE(status.find("\"state\": \"sandbox_failed\"") != std::string::npos);
    EXPECT_TRUE(status.find("\"reason\":\"signal\"") != std::string::npos);

    remove_all({"cand_segv.so", "active_segv.so", "t_segv.status.json", "t_segv.log", "cand_segv.so.cpp"});
}

TEST_CASE(infinite_loop_candidate_is_killed_near_the_timeout) {
    build_plugin(INFINITE_LOOP_PLUGIN, "cand_loop.so");
    write_file("active_loop.so", "KNOWN-GOOD");

    auto start = std::chrono::steady_clock::now();
    bool ok = validate_candidate("t_loop", "./cand_loop.so", "./active_loop.so", 200);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

    EXPECT_FALSE(ok);
    EXPECT_TRUE(elapsed_ms < 1000); // killed close to the 200ms budget, not hung
    EXPECT_EQ(read_file("./active_loop.so"), std::string("KNOWN-GOOD"));
    EXPECT_TRUE(read_file("t_loop.status.json").find("\"state\": \"sandbox_timeout\"") != std::string::npos);

    remove_all({"cand_loop.so", "active_loop.so", "t_loop.status.json", "t_loop.log", "cand_loop.so.cpp"});
}

TEST_CASE(throwing_candidate_is_rejected) {
    build_plugin(THROWING_PLUGIN, "cand_throw.so");
    write_file("active_throw.so", "KNOWN-GOOD");

    bool ok = validate_candidate("t_throw", "./cand_throw.so", "./active_throw.so", 2000);

    EXPECT_FALSE(ok);
    EXPECT_EQ(read_file("./active_throw.so"), std::string("KNOWN-GOOD"));
    EXPECT_TRUE(read_file("t_throw.status.json").find("\"state\": \"sandbox_failed\"") != std::string::npos);

    remove_all({"cand_throw.so", "active_throw.so", "t_throw.status.json", "t_throw.log", "cand_throw.so.cpp"});
}

TEST_CASE(candidate_missing_plugin_update_symbol_is_rejected) {
    build_plugin(MISSING_SYMBOL_PLUGIN, "cand_missing.so");
    write_file("active_missing.so", "KNOWN-GOOD");

    bool ok = validate_candidate("t_missing", "./cand_missing.so", "./active_missing.so", 2000);

    EXPECT_FALSE(ok);
    EXPECT_EQ(read_file("./active_missing.so"), std::string("KNOWN-GOOD"));
    std::string status = read_file("t_missing.status.json");
    EXPECT_TRUE(status.find("\"state\": \"sandbox_failed\"") != std::string::npos);
    EXPECT_TRUE(status.find("exit_code") != std::string::npos);

    remove_all({"cand_missing.so", "active_missing.so", "t_missing.status.json", "t_missing.log", "cand_missing.so.cpp"});
}

TEST_CASE(nonexistent_candidate_path_is_rejected_without_crashing_the_runner) {
    write_file("active_none.so", "KNOWN-GOOD");

    bool ok = validate_candidate("t_none", "./does_not_exist.so", "./active_none.so", 2000);

    EXPECT_FALSE(ok);
    EXPECT_EQ(read_file("./active_none.so"), std::string("KNOWN-GOOD"));

    remove_all({"active_none.so", "t_none.status.json", "t_none.log"});
}

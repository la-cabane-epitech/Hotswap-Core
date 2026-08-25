#include "test_runner.hpp"
#include "../StatusWriter.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

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

} // namespace

TEST_CASE(write_status_creates_final_file_and_no_leftover_tmp) {
    const std::string path = "test_status_a.json";
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());

    write_status(path, "modA", "sandbox_running", "sandbox",
                 "cand.so", "active.so", "{}", "modA.log");

    EXPECT_TRUE(file_exists(path));
    EXPECT_FALSE(file_exists(path + ".tmp"));

    std::remove(path.c_str());
}

TEST_CASE(write_status_contains_all_schema_fields) {
    const std::string path = "test_status_b.json";
    std::remove(path.c_str());

    write_status(path, "plugin", "sandbox_failed", "sandbox",
                 "./libplugin.so.candidate", "./libplugin.so",
                 "{\"reason\":\"signal\",\"signal\":\"Segmentation fault\"}",
                 "plugin.log");

    std::string content = read_file(path);
    EXPECT_TRUE(content.find("\"schema_version\": 1") != std::string::npos);
    EXPECT_TRUE(content.find("\"module\": \"plugin\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"state\": \"sandbox_failed\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"producer\": \"sandbox\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"candidate_path\": \"./libplugin.so.candidate\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"active_path\": \"./libplugin.so\"") != std::string::npos);
    EXPECT_TRUE(content.find("Segmentation fault") != std::string::npos);
    EXPECT_TRUE(content.find("\"log_path\": \"plugin.log\"") != std::string::npos);

    std::remove(path.c_str());
}

TEST_CASE(write_status_overwrite_leaves_only_the_latest_state) {
    const std::string path = "test_status_c.json";
    std::remove(path.c_str());

    write_status(path, "plugin", "sandbox_running", "sandbox", "c", "a", "{}", "l");
    write_status(path, "plugin", "sandbox_passed", "sandbox", "c", "a",
                 "{\"duration_ms\":42}", "l");

    std::string content = read_file(path);
    EXPECT_TRUE(content.find("\"state\": \"sandbox_passed\"") != std::string::npos);
    EXPECT_TRUE(content.find("sandbox_running") == std::string::npos);

    std::remove(path.c_str());
}

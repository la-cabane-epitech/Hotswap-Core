/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Runtime tests — every path of a swap, against real shared libraries
*/

#include <unistd.h>

#include <filesystem>
#include <string>

#include "DLLoader.hpp"
#include "check.hpp"

namespace fs = std::filesystem;

namespace {

/* A private artifact directory per test, removed afterwards. */
struct Sandbox {
    fs::path    dir;
    std::string active;
    std::string candidate;

    Sandbox()
    {
        static int count = 0;
        dir = fs::temp_directory_path() /
              ("hotswap_test_" + std::to_string(getpid()) + "_" + std::to_string(count++));
        fs::remove_all(dir);
        fs::create_directories(dir);
        active = (dir / "libplugin.so").string();
        candidate = active + ".candidate";
    }

    ~Sandbox()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void install(const char *plugin, const std::string &as) const
    {
        fs::copy_file(plugin, as, fs::copy_options::overwrite_existing);
    }

    size_t generation_files() const
    {
        size_t count = 0;
        for (const auto &entry : fs::directory_iterator(dir))
            count += entry.path().filename().string().find(".gen") != std::string::npos;
        return count;
    }
};

int counter_of(const DLLoader &runtime)
{
    using CounterFn = int (*)(const void *);
    auto read = reinterpret_cast<CounterFn>(runtime.symbol("test_counter"));
    return read != nullptr ? read(runtime.state()) : -1;
}

void run_updates(DLLoader &runtime, int count)
{
    for (int i = 0; i < count; ++i)
        runtime.update();
}

} // namespace

TEST_CASE(initial_load_creates_a_fresh_state)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);

    DLLoader runtime(box.active, box.candidate);
    runtime.poll();

    REQUIRE(runtime.is_loaded());
    CHECK(runtime.state() != nullptr);
    CHECK(counter_of(runtime) == 0);
    run_updates(runtime, 3);
    CHECK(counter_of(runtime) == 3);
}

TEST_CASE(nothing_to_load_stays_unloaded)
{
    Sandbox box;
    DLLoader runtime(box.active, box.candidate);

    runtime.poll();
    CHECK(!runtime.is_loaded());
    runtime.update(); /* must be a harmless no-op */
}

TEST_CASE(candidate_without_active_is_loaded)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.candidate);

    DLLoader runtime(box.active, box.candidate);
    runtime.poll();

    REQUIRE(runtime.is_loaded());
    CHECK(counter_of(runtime) == 0);
    CHECK(!fs::exists(box.candidate));
    CHECK(fs::exists(box.active));
}

TEST_CASE(same_layout_keeps_the_very_same_state)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);
    DLLoader runtime(box.active, box.candidate);
    runtime.poll();
    run_updates(runtime, 3);
    void *before = runtime.state();

    box.install(TEST_PLUGIN_V1_STEP10, box.candidate);
    runtime.poll();

    CHECK(runtime.state() == before);
    CHECK(counter_of(runtime) == 3);
    runtime.update();
    CHECK(counter_of(runtime) == 13); /* the new code runs on the old state */
    CHECK(!fs::exists(box.candidate));
}

TEST_CASE(layout_change_migrates_through_a_snapshot)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);
    DLLoader runtime(box.active, box.candidate);
    runtime.poll();
    run_updates(runtime, 3);
    void *before = runtime.state();

    box.install(TEST_PLUGIN_V2_PADDED, box.candidate);
    runtime.poll();

    CHECK(runtime.state() != before);
    CHECK(counter_of(runtime) == 3);
    runtime.update();
    CHECK(counter_of(runtime) == 4);
}

TEST_CASE(rejected_snapshot_keeps_the_running_version)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);
    DLLoader runtime(box.active, box.candidate);
    runtime.poll();
    run_updates(runtime, 3);
    void *before = runtime.state();

    box.install(TEST_PLUGIN_LOAD_FAILS, box.candidate);
    runtime.poll();

    REQUIRE(runtime.is_loaded());
    CHECK(runtime.state() == before);
    CHECK(counter_of(runtime) == 3);
    runtime.update();
    CHECK(counter_of(runtime) == 4); /* still the v1 code, step 1 */
    CHECK(!fs::exists(box.candidate));
    CHECK(box.generation_files() == 0);
}

TEST_CASE(abi_mismatch_is_rejected)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);
    DLLoader runtime(box.active, box.candidate);
    runtime.poll();
    run_updates(runtime, 2);

    box.install(TEST_PLUGIN_ABI_MISMATCH, box.candidate);
    runtime.poll();

    REQUIRE(runtime.is_loaded());
    CHECK(counter_of(runtime) == 2);
    CHECK(!fs::exists(box.candidate));
}

TEST_CASE(missing_symbol_is_rejected)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);
    DLLoader runtime(box.active, box.candidate);
    runtime.poll();
    run_updates(runtime, 2);

    box.install(TEST_PLUGIN_MISSING_LOAD, box.candidate);
    runtime.poll();

    REQUIRE(runtime.is_loaded());
    CHECK(counter_of(runtime) == 2);
}

TEST_CASE(broken_active_is_not_retried_until_it_changes)
{
    Sandbox box;
    box.install(TEST_PLUGIN_MISSING_LOAD, box.active);
    DLLoader runtime(box.active, box.candidate);

    runtime.poll();
    runtime.poll();
    CHECK(!runtime.is_loaded());

    box.install(TEST_PLUGIN_V1, box.candidate);
    runtime.poll();
    CHECK(runtime.is_loaded());
}

TEST_CASE(successive_swaps_leave_one_generation_file)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);
    DLLoader runtime(box.active, box.candidate);
    runtime.poll();

    const char *versions[] = {TEST_PLUGIN_V1_STEP10, TEST_PLUGIN_V2_PADDED, TEST_PLUGIN_V1};
    for (const char *version : versions) {
        box.install(version, box.candidate);
        runtime.poll();
        runtime.update();
        CHECK(box.generation_files() == 1);
    }
    /* 0 → +10 → migrated to v2, +1 → migrated back to v1, +1 */
    CHECK(counter_of(runtime) == 12);
}

TEST_CASE(restart_starts_from_the_latest_promoted_version)
{
    Sandbox box;
    box.install(TEST_PLUGIN_V1, box.active);
    {
        DLLoader runtime(box.active, box.candidate);
        runtime.poll();
        box.install(TEST_PLUGIN_V1_STEP10, box.candidate);
        runtime.poll();
    }
    CHECK(box.generation_files() == 0);

    DLLoader restarted(box.active, box.candidate);
    restarted.poll();
    REQUIRE(restarted.is_loaded());
    restarted.update();
    CHECK(counter_of(restarted) == 10); /* the step-10 build, not the original */
}

int main()
{
    return check::run_all();
}

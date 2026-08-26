#!/usr/bin/env bash
#
# Hotswap-Core MVP demo.
#
#   ./demo.sh              normal pace (7 s pauses, room to narrate)
#   STEP=3 ./demo.sh       fast pace, for rehearsing
#
# Starts the Runtime and the Watcher, then rewrites the plugin under their feet
# to walk through four scenarios. Each one covers a failure mode the compiler
# cannot report and that would otherwise kill the host process:
#
#   1. valid change      -> canary passes, code is swapped, session state kept
#   2. null dereference  -> canary rejects (signal),  host survives
#   3. infinite loop     -> canary rejects (timeout), host survives
#   4. compile error     -> build fails, no candidate is ever produced
#
# The point of the demo is the counter printed by the plugin: it never restarts,
# which proves the host process was never restarted either.
#
# plugin.cpp is restored on exit, Ctrl+C included.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PLUGIN="src/plugin/plugin.cpp"
ARTIFACTS=".hotswap"
BACKUP="$(mktemp)"
STEP="${STEP:-7}"
HOST_PID=""
WATCH_PID=""

BOLD=$'\033[1m'; CYAN=$'\033[1;36m'; GREEN=$'\033[1;32m'; RED=$'\033[1;31m'; OFF=$'\033[0m'

# Runs on any exit path, so a Ctrl+C in the middle of a scenario cannot leave a
# deliberately broken plugin behind.
cleanup() {
    [ -n "$HOST_PID" ]  && kill -9 "$HOST_PID"  2>/dev/null
    [ -n "$WATCH_PID" ] && kill -9 "$WATCH_PID" 2>/dev/null
    [ -f "$BACKUP" ] && cp "$BACKUP" "$PLUGIN" && rm -f "$BACKUP"
    rm -f "$ARTIFACTS"/*.candidate "$ARTIFACTS"/*.previous "$ARTIFACTS"/*.tmp
    printf '\n%s--- demo over, %s restored ---%s\n' "$BOLD" "$PLUGIN" "$OFF"
}
trap cleanup EXIT INT TERM

say() { printf '\n%s>>> %s%s\n' "$CYAN" "$*" "$OFF"; }
wait_step() { sleep "$STEP"; }

cp "$PLUGIN" "$BACKUP"

say "Building the project"
./build.sh >/dev/null 2>&1 || { echo "build failed"; exit 1; }
# A candidate left over from a previous run would be picked up immediately and
# blur the first scenario.
rm -f "$ARTIFACTS"/*.candidate "$ARTIFACTS"/*.previous

say "Starting the Runtime and the Watcher"
# disown keeps the shell from printing "Killed: 9" when cleanup kills them.
./main &        HOST_PID=$!;  disown "$HOST_PID"  2>/dev/null
./FileWatcher & WATCH_PID=$!; disown "$WATCH_PID" 2>/dev/null
sleep 3

# --- 1 --------------------------------------------------------------------
# Only the printed label changes. Watch the counter: it must carry on from
# where the previous version left it, never restart.
say "SCENARIO 1 — valid plugin change"
echo "    The counter must NOT restart from scratch after the swap."
sed -i.bak 's/Plugin v1/Plugin v2/' "$PLUGIN" && rm -f "$PLUGIN.bak"
wait_step

# --- 2 --------------------------------------------------------------------
# Compiles cleanly. Loaded directly into the host, this would take the whole
# process down along with the session state.
say "SCENARIO 2 — the plugin segfaults  ${RED}(null dereference)${OFF}"
echo "    Expected: canary rejected (signal), and the Runtime SURVIVES."
cat > "$PLUGIN" <<'EOF'
#include "plugin.hpp"
#include <iostream>
void plugin_update(State *state)
{
    state->counter++;
    std::cout << "[Plugin BROKEN] " << state->counter << std::endl;
    int *p = nullptr;
    *p = 42;
}
EOF
wait_step

# --- 3 --------------------------------------------------------------------
# Also compiles cleanly, and would freeze the host forever: the reload loop
# would never get control back. Caught by the canary's timer, so this scenario
# needs a couple of extra seconds.
say "SCENARIO 3 — the plugin spins in an infinite loop"
echo "    Expected: canary rejected (timeout) after 2 s, the Runtime SURVIVES."
cat > "$PLUGIN" <<'EOF'
#include "plugin.hpp"
void plugin_update(State *state) { (void)state; while (true) {} }
EOF
sleep $((STEP + 2))

# --- 4 --------------------------------------------------------------------
# The only failure the compiler does catch. It never reaches the canary: with
# no candidate produced, there is nothing to validate.
say "SCENARIO 4 — the plugin does not compile"
echo "    Expected: build FAILED, no candidate produced, the Runtime SURVIVES."
cat > "$PLUGIN" <<'EOF'
#include "plugin.hpp"
void plugin_update(State *state) { state->no_such_field = 1; }
EOF
wait_step

# --- 5 --------------------------------------------------------------------
# Back to a working plugin: the host picks it up like any other candidate,
# still on the same counter.
say "SCENARIO 5 — back to a valid plugin"
cp "$BACKUP" "$PLUGIN"
touch "$PLUGIN"
wait_step

printf '\n%s================= RESULT =================%s\n' "$BOLD" "$OFF"
if kill -0 "$HOST_PID" 2>/dev/null; then
    printf '%s  The Runtime survived all 3 failures.%s\n' "$GREEN" "$OFF"
    printf "  It never restarted: the counter above never went back to its initial value.\n"
else
    printf '%s  The Runtime died — the demo failed.%s\n' "$RED" "$OFF"
fi
printf '%s==========================================%s\n' "$BOLD" "$OFF"

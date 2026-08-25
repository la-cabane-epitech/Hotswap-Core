#pragma once

#include <string>

// Validates a freshly built plugin candidate before it is trusted:
//   1. forks an isolated child process that dlopen()s the candidate and
//      calls plugin_update() once on a throwaway State,
//   2. kills the child if it hasn't reported back within timeout_ms,
//   3. on success, atomically promotes candidate_path -> active_path
//      (the Runtime's DLLoader only ever reloads active_path, so a
//      rejected candidate simply never overwrites the last known-good
//      version).
//
// Writes <module_name>.status.json at every step (schema in the top-level
// README) so Reporting can observe the pipeline without polling this
// process directly.
//
// Returns true iff the candidate passed and was promoted.
bool validate_candidate(const std::string& module_name,
                         const std::string& candidate_path,
                         const std::string& active_path,
                         int timeout_ms);

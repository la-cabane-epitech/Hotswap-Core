#pragma once

#include <string>

// Writes <status_path> atomically (write to "<status_path>.tmp", then rename
// over it) so a reader polling the file's mtime never observes a half-written
// JSON. Schema documented in the repo's top-level README ("Protocole de
// statut (inter-composants)").
void write_status(const std::string& status_path,
                   const std::string& module_name,
                   const std::string& state,
                   const std::string& producer,
                   const std::string& candidate_path,
                   const std::string& active_path,
                   const std::string& detail_json,
                   const std::string& log_path);

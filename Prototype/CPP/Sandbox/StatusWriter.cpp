#include "StatusWriter.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>

namespace {

std::string now_iso8601_utc() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace

void write_status(const std::string& status_path,
                   const std::string& module_name,
                   const std::string& state,
                   const std::string& producer,
                   const std::string& candidate_path,
                   const std::string& active_path,
                   const std::string& detail_json,
                   const std::string& log_path) {
    std::string tmp_path = status_path + ".tmp";

    std::ofstream out(tmp_path, std::ios::trunc);
    out << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"module\": \"" << module_name << "\",\n"
        << "  \"state\": \"" << state << "\",\n"
        << "  \"producer\": \"" << producer << "\",\n"
        << "  \"timestamp\": \"" << now_iso8601_utc() << "\",\n"
        << "  \"candidate_path\": \"" << candidate_path << "\",\n"
        << "  \"active_path\": \"" << active_path << "\",\n"
        << "  \"detail\": " << detail_json << ",\n"
        << "  \"log_path\": \"" << log_path << "\"\n"
        << "}\n";
    out.close();

    std::rename(tmp_path.c_str(), status_path.c_str());
}

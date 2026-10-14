
#include "proc_reader.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace safebox {
namespace controller {
namespace {

bool parseLongField(const std::string& text, long& value) {
    char* end = nullptr;
    errno = 0;
    value = std::strtol(text.c_str(), &end, 10);
    return errno == 0 && end != text.c_str() && *end == '\0';
}

}
bool parseProcessStatLine(const std::string& line, pid_t pid, ProcessInfo& info,
                          std::string& error) {
    size_t close_name = line.rfind(')');
    size_t open_name = line.find('(');
    if (open_name == std::string::npos || close_name == std::string::npos ||
        close_name <= open_name || close_name + 2 >= line.size()) {
        error = "invalid /proc stat process name";
        return false;
    }
    std::istringstream fields(line.substr(close_name + 2));
    std::vector<std::string> values;
    std::string value;
    while (fields >> value) values.push_back(value);
    long parent = 0;
    long process_group = 0;
    long user_ticks = 0;
    long system_ticks = 0;
    if (values.size() <= 12 || !parseLongField(values[1], parent) ||
        !parseLongField(values[2], process_group) ||
        !parseLongField(values[11], user_ticks) || !parseLongField(values[12], system_ticks)) {
        error = "invalid /proc stat counters";
        return false;
    }
    long ticks_per_second = sysconf(_SC_CLK_TCK);
    if (ticks_per_second <= 0 || parent < 0 || process_group < 0 ||
        user_ticks < 0 || system_ticks < 0) {
        error = "invalid process counters or clock tick rate";
        return false;
    }
    info.pid = pid;
    info.parent_pid = static_cast<pid_t>(parent);
    info.process_group_id = static_cast<pid_t>(process_group);
    info.state = values[0][0];
    info.name = line.substr(open_name + 1, close_name - open_name - 1);
    info.cpu_ms = (user_ticks + system_ticks) * 1000L / ticks_per_second;
    error.clear();
    return true;
}

bool readProcessInfo(pid_t pid, ProcessInfo& info, std::string& error) {
    if (pid <= 0) {
        error = "PID must be positive";
        return false;
    }
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream input(path.c_str());
    std::string line;
    if (!input || !std::getline(input, line)) {
        error = "cannot read " + path;
        return false;
    }
    return parseProcessStatLine(line, pid, info, error);
}

}}
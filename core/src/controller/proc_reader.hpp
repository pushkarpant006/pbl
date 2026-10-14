
#pragma once

#include <string>
#include <sys/types.h>

namespace safebox {
namespace controller {

struct ProcessInfo {
    pid_t pid{0};
    pid_t parent_pid{0};
    pid_t process_group_id{0};
    char state{'?'};
    std::string name;
    long cpu_ms{0};
};

bool parseProcessStatLine(const std::string& line, pid_t pid, ProcessInfo& info,
                          std::string& error);
bool readProcessInfo(pid_t pid, ProcessInfo& info, std::string& error);

}}

#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

#include "process_table.hpp"

namespace safebox {
namespace controller {

struct ProcessChanges {
    std::vector<pid_t> added_pids;
    std::vector<pid_t> exited_pids;
    std::vector<std::string> errors;
};

class ProcessTree {
public:
    ProcessTree(ProcessTable& table, pid_t root_pid);
    ProcessChanges refreshTable();

private:
    ProcessTable& table_;
    pid_t root_pid_;
};

}}
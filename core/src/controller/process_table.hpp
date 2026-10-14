
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace safebox {
namespace controller {

enum class ProcessState {
    NEW,
    READY,
    RUNNING,
    SUSPENDED,
    TERMINATED
};

struct PCB {
    pid_t pid{0};
    pid_t parent_pid{0};
    std::string name;
    ProcessState state{ProcessState::NEW};
    std::chrono::steady_clock::time_point arrival_time;
    std::optional<std::chrono::steady_clock::time_point> first_run_time;
    std::optional<std::chrono::steady_clock::time_point> finish_time;
    std::optional<int> exit_status;
    long total_cpu_ms{0};
    long remaining_cpu_ms{0};
    long cpu_ms{0};
    int priority{0};
    int queue_level{0};
};

class ProcessTable {
public:
    bool addProcess(const PCB& process);
    PCB* findProcess(pid_t pid);
    const PCB* findProcess(pid_t pid) const;
    bool setState(pid_t pid, ProcessState next_state);
    bool canTransition(ProcessState current_state, ProcessState next_state) const;
    const std::vector<PCB>& processes() const;

private:
    std::vector<PCB> processes_;
};

const char* processStateName(ProcessState state);

}}

#include "process_table.hpp"

namespace safebox {
namespace controller {

bool ProcessTable::addProcess(const PCB& process) {
    if (process.pid <= 0 || findProcess(process.pid) != nullptr) {
        return false;
    }
    processes_.push_back(process);
    return true;
}

PCB* ProcessTable::findProcess(pid_t pid) {
    for (int index = 0; index < static_cast<int>(processes_.size()); index++) {
        if (processes_[index].pid == pid) return &processes_[index];
    }
    return nullptr;
}

const PCB* ProcessTable::findProcess(pid_t pid) const {
    for (int index = 0; index < static_cast<int>(processes_.size()); index++) {
        if (processes_[index].pid == pid) return &processes_[index];
    }
    return nullptr;
}

bool ProcessTable::setState(pid_t pid, ProcessState next_state) {
    PCB* process = findProcess(pid);
    if (process == nullptr || !canTransition(process->state, next_state)) return false;
    process->state = next_state;
    if (next_state == ProcessState::RUNNING && !process->first_run_time) {
        process->first_run_time = std::chrono::steady_clock::now();
    }
    if (next_state == ProcessState::TERMINATED) {
        process->finish_time = std::chrono::steady_clock::now();
    }
    return true;
}

bool ProcessTable::canTransition(ProcessState current_state, ProcessState next_state) const {
    if (current_state == next_state) return true;
    if (next_state == ProcessState::TERMINATED) return current_state != ProcessState::TERMINATED;
    if (current_state == ProcessState::NEW) return next_state == ProcessState::READY;
    if (current_state == ProcessState::READY) {
        return next_state == ProcessState::RUNNING || next_state == ProcessState::SUSPENDED;
    }
    if (current_state == ProcessState::RUNNING) {
        return next_state == ProcessState::READY || next_state == ProcessState::SUSPENDED;
    }
    return current_state == ProcessState::SUSPENDED && next_state == ProcessState::READY;
}

const std::vector<PCB>& ProcessTable::processes() const {
    return processes_;
}

const char* processStateName(ProcessState state) {
    if (state == ProcessState::NEW) return "NEW";
    if (state == ProcessState::READY) return "READY";
    if (state == ProcessState::RUNNING) return "RUNNING";
    if (state == ProcessState::SUSPENDED) return "SUSPENDED";
    return "TERMINATED";
}

}}
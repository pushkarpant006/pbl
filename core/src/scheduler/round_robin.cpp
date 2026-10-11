// CONCEPT: Round robin scheduling
// WHY SAFEBOX NEEDS IT: A FIFO ready list gives each process a bounded turn so one
// CPU-bound process cannot keep all other ready processes waiting.
// HOW IT WORKS: The scheduler removes the front PID for execution and appends it
// after a quantum if work remains. Finished PIDs are removed from scheduler state.

#include "round_robin.hpp"

using namespace std;

namespace safebox {
namespace scheduler {

// Remember the quantum that limits each process turn.
RoundRobinScheduler::RoundRobinScheduler(long quantum_ms) {
    quantum_ms_ = quantum_ms;
}

// Store a process and add its PID to the ready list only once.
void RoundRobinScheduler::addProcess(const controller::PCB& process) {
    processes_[process.pid] = process;
    for (int index = 0; index < static_cast<int>(ready_queue_.size()); index++) {
        if (ready_queue_[index] == process.pid) {
            return;
        }
    }
    ready_queue_.push_back(process.pid);
}

// Take the first ready PID so the runner can give it the CPU.
optional<pid_t> RoundRobinScheduler::pickNext() {
    if (ready_queue_.empty()) {
        return nullopt;
    }
    pid_t pid = ready_queue_[0];
    for (int index = 1; index < static_cast<int>(ready_queue_.size()); index++) {
        ready_queue_[index - 1] = ready_queue_[index];
    }
    ready_queue_.pop_back();
    return pid;
}

// Give the process no more than its estimated remaining CPU time.
long RoundRobinScheduler::timeSliceMs(pid_t pid) {
    map<pid_t, controller::PCB>::iterator process = processes_.find(pid);
    long slice_ms = quantum_ms_;
    if (process != processes_.end() && process->second.remaining_cpu_ms > 0 &&
        process->second.remaining_cpu_ms < slice_ms) {
        slice_ms = process->second.remaining_cpu_ms;
    }
    if (slice_ms <= 0) {
        slice_ms = 1;
    }
    active_slice_ms_[pid] = slice_ms;
    return slice_ms;
}

// Account for one estimated slice and return the PID to the end of the ready list.
void RoundRobinScheduler::onQuantumExpired(pid_t pid) {
    map<pid_t, controller::PCB>::iterator process = processes_.find(pid);
    if (process == processes_.end()) {
        return;
    }
    long elapsed_ms = active_slice_ms_[pid];
    if (elapsed_ms <= 0) {
        elapsed_ms = quantum_ms_;
    }
    process->second.remaining_cpu_ms -= elapsed_ms;
    if (process->second.remaining_cpu_ms < 0) {
        process->second.remaining_cpu_ms = 0;
    }
    active_slice_ms_.erase(pid);
    ready_queue_.push_back(pid);
}

// Erase a finished PID from both the ready list and scheduler records.
void RoundRobinScheduler::onProcessFinished(pid_t pid) {
    vector<pid_t> remaining;
    for (int index = 0; index < static_cast<int>(ready_queue_.size()); index++) {
        if (ready_queue_[index] != pid) {
            remaining.push_back(ready_queue_[index]);
        }
    }
    ready_queue_ = remaining;
    processes_.erase(pid);
    active_slice_ms_.erase(pid);
}

// Tell the runner whether there is still a process waiting for CPU.
bool RoundRobinScheduler::hasReadyProcess() const {
    return !ready_queue_.empty();
}

// Return the policy name shown in diagnostics.
string RoundRobinScheduler::name() const {
    return "round_robin";
}

}  // namespace scheduler
}  // namespace safebox

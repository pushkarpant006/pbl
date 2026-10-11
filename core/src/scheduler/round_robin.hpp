// CONCEPT: Round robin scheduling
// WHY SAFEBOX NEEDS IT: A fair scheduler lets each process run for a bounded time,
// which prevents one process from starving the rest of the CPU. Round robin is a
// readable baseline policy for SafeBox and is easy to explain in class.
// HOW IT WORKS: The scheduler keeps a FIFO queue of ready processes. Each process
// receives one quantum; if it still needs more CPU, it is placed at the end of the
// queue so the next process can run.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

#include "scheduler.hpp"

namespace safebox {
namespace scheduler {

struct RoundRobinScheduler : Scheduler {
    // Keep the supplied quantum as the maximum length of each CPU slice.
    explicit RoundRobinScheduler(long quantum_ms);

    // Store a process and add its PID to the ready queue once.
    void addProcess(const controller::PCB& process) override;
    // Return and remove the PID at the front of the ready queue.
    std::optional<pid_t> pickNext() override;
    // Return the smaller of the configured quantum and estimated CPU remaining.
    long timeSliceMs(pid_t pid) override;
    // Account for one slice and return unfinished work to the queue.
    void onQuantumExpired(pid_t pid) override;
    // Remove a PID after the runner confirms process completion.
    void onProcessFinished(pid_t pid) override;
    // Check whether a process is waiting for a CPU slice.
    bool hasReadyProcess() const override;
    // Give the policy a readable diagnostic name.
    std::string name() const override;

    // Scheduler queue and per-process state are kept outside the shared PCB.
    long quantum_ms_;
    std::vector<pid_t> ready_queue_;
    std::map<pid_t, controller::PCB> processes_;
    std::map<pid_t, long> active_slice_ms_;
};

}  // namespace scheduler
}  // namespace safebox

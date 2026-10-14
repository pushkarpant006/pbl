
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
    explicit RoundRobinScheduler(long quantum_ms);

    void addProcess(const controller::PCB& process) override;
    std::optional<pid_t> pickNext() override;
    long timeSliceMs(pid_t pid) override;
    void onQuantumExpired(pid_t pid) override;
    void onProcessFinished(pid_t pid) override;
    bool hasReadyProcess() const override;
    std::string name() const override;

    long quantum_ms_;
    std::vector<pid_t> ready_queue_;
    std::map<pid_t, controller::PCB> processes_;
    std::map<pid_t, long> active_slice_ms_;
};

}}
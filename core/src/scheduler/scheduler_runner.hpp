
#pragma once

#include <vector>

#include "scheduler.hpp"
#include "scheduler_metrics.hpp"

namespace safebox {
namespace scheduler {

struct SchedulerRunner {
    SchedulerRunner(Scheduler& scheduler, const std::vector<pid_t>& pids, long quantum_ms);

    void run();
    const std::vector<SliceRecord>& slices() const;

    Scheduler& scheduler_;
    std::vector<pid_t> pids_;
    long quantum_ms_;
    SchedulerMetrics metrics_;
};

}}
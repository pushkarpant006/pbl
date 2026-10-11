// CONCEPT: Scheduler runner
// WHY SAFEBOX NEEDS IT: A queue alone is not enough; SafeBox must actually resume a
// real process, observe it for one quantum, and stop it again. This is the bridge
// between the algorithm and the Linux kernel.
// HOW IT WORKS: The runner takes already-launched child PIDs, resumes one process,
// waits for the quantum to elapse, stops it again, and records the amount of CPU it
// consumed before moving to the next process.

#pragma once

#include <vector>

#include "scheduler.hpp"
#include "scheduler_metrics.hpp"

namespace safebox {
namespace scheduler {

struct SchedulerRunner {
    // Connect one scheduler policy to the already-launched process IDs.
    SchedulerRunner(Scheduler& scheduler, const std::vector<pid_t>& pids, long quantum_ms);

    // Run slices until every ready process has completed or failed.
    void run();
    // Let tests inspect the timeline collected during the run.
    const std::vector<SliceRecord>& slices() const;

    // References and process IDs are supplied by the caller.
    Scheduler& scheduler_;
    std::vector<pid_t> pids_;
    long quantum_ms_;
    SchedulerMetrics metrics_;
};

}  // namespace scheduler
}  // namespace safebox

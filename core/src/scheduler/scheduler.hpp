// CONCEPT: Scheduler interface
// WHY SAFEBOX NEEDS IT: The OS must be able to swap between processes without
// tying one algorithm to a specific implementation. A common scheduler contract lets
// the controller, metrics, and runner work with any policy such as round robin or
// priority scheduling.
// HOW IT WORKS: The interface defines the small set of operations a scheduler must
// support: add a process, choose the next ready process, react to quantum expiry, and
// report when a process finishes.

#pragma once

#include <optional>
#include <string>
#include <sys/types.h>

#include "../controller/process_table.hpp"

namespace safebox {
namespace scheduler {

class Scheduler {
public:
    // Allow use through a base reference without prescribing scheduler storage.
    virtual ~Scheduler() = default;

    // Add one process to the policy's ready set.
    virtual void addProcess(const controller::PCB& process) = 0;
    // Select and remove the next ready PID, or report that the queue is empty.
    virtual std::optional<pid_t> pickNext() = 0;
    // Return the next CPU budget for this process in milliseconds.
    virtual long timeSliceMs(pid_t pid) = 0;
    // Requeue a process after its current time slice expires.
    virtual void onQuantumExpired(pid_t pid) = 0;
    // Remove a process that the runner has confirmed has ended.
    virtual void onProcessFinished(pid_t pid) = 0;
    // Report whether any process is ready to run.
    virtual bool hasReadyProcess() const = 0;
    // Return a readable policy name for diagnostics.
    virtual std::string name() const = 0;
};

}  // namespace scheduler
}  // namespace safebox

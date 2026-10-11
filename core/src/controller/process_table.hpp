// CONCEPT: Process control block (PCB)
// WHY SAFEBOX NEEDS IT: The scheduler and controller must both refer to the same
// process identity and lifecycle state without duplicating rules in each module. The
// PCB is the shared contract for process ownership, CPU accounting, and state
// transitions in a Linux sandbox.
// HOW IT WORKS: The controller owns the PCB definition and exposes a small, stable
// set of fields. The scheduler reads these fields to decide who runs next, but it
// does not store its own queue state inside the PCB.

#ifndef SAFEBOX_PROCESS_TABLE_HPP
#define SAFEBOX_PROCESS_TABLE_HPP

#include <chrono>
#include <optional>
#include <sys/types.h>

namespace safebox {
namespace controller {

enum class ProcessState {
    NEW,
    // READY means the process is waiting for CPU, including a process that was
    // temporarily stopped by the scheduler between quanta.
    READY,
    RUNNING,
    // SUSPENDED means SafeBox paused the process because it detected suspicious
    // behaviour and is waiting for the user's decision.
    SUSPENDED,
    TERMINATED
};

struct PCB {
    pid_t pid{0};
    ProcessState state{ProcessState::NEW};

    std::chrono::steady_clock::time_point arrival_time;
    std::optional<std::chrono::steady_clock::time_point> first_run_time;
    std::optional<std::chrono::steady_clock::time_point> finish_time;

    long total_cpu_ms{0};
    long remaining_cpu_ms{0};
    int priority{0};
    int queue_level{0};
};

}  // namespace controller
}  // namespace safebox

#endif  // SAFEBOX_PROCESS_TABLE_HPP

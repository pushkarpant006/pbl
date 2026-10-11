// CONCEPT: Scheduler metrics
// WHY SAFEBOX NEEDS IT: A scheduler is not just a queue; it must show whether it is
// fair, responsive, and complete. SafeBox needs numbers that a team can present in a
// viva or discuss in a report.
// HOW IT WORKS: The metrics object records each quantum slice, then turns the raw
// timeline into waiting, turnaround, response, and context-switch figures in a clear
// table.

#pragma once

#include <optional>
#include <sys/types.h>
#include <string>
#include <vector>

namespace safebox {
namespace scheduler {

struct SliceRecord {
    pid_t pid{0};
    long quantum_ms{0};
    long long start_ms{0};
    long long end_ms{0};
    long long cpu_before_ns{0};
    long long cpu_after_ns{0};
};

struct ProcessMetrics {
    pid_t pid{0};
    long long arrival_ms{0};
    std::optional<long long> first_run_ms;
    std::optional<long long> finish_ms;
    long long first_cpu_ns{0};
    long long total_cpu_ns{0};
    int slices{0};
    std::optional<int> exit_status;
};

struct SchedulerMetrics {
    // Register one process's arrival relative to scheduler start.
    void addProcess(pid_t pid, long long arrival_ms);
    // Store one measured CPU slice and update the process totals.
    void addSlice(pid_t pid, long quantum_ms, long long start_ms, long long end_ms,
                  long long cpu_before_ns, long long cpu_after_ns);
    // Store the normal exit status and final CPU-clock reading.
    void markFinished(pid_t pid, long long finish_ms, int exit_status, long long final_cpu_ns);
    // Record why a process could not complete normally.
    void markTerminated(pid_t pid, long long finish_ms, const std::string& reason);
    // Print each slice as an elapsed-time bar.
    void printTrace() const;
    // Print per-process timing, CPU, and context-switch totals.
    void printSummary() const;
    // Expose recorded slices to tests without copying them.
    const std::vector<SliceRecord>& slices() const;

private:
    std::vector<SliceRecord> slices_;
    std::vector<ProcessMetrics> processes_;
    std::vector<std::string> termination_reasons_;
};

}  // namespace scheduler
}  // namespace safebox


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
    void addProcess(pid_t pid, long long arrival_ms);
    void addSlice(pid_t pid, long quantum_ms, long long start_ms, long long end_ms,
                  long long cpu_before_ns, long long cpu_after_ns);
    void markFinished(pid_t pid, long long finish_ms, int exit_status, long long final_cpu_ns);
    void markTerminated(pid_t pid, long long finish_ms, const std::string& reason);
    void printTrace() const;
    void printSummary() const;
    const std::vector<SliceRecord>& slices() const;
    const std::vector<ProcessMetrics>& processes() const;
    int contextSwitchCount() const;

private:
    std::vector<SliceRecord> slices_;
    std::vector<ProcessMetrics> processes_;
    std::vector<std::string> termination_reasons_;
};

}}
// CONCEPT: Scheduler metrics
// WHY SAFEBOX NEEDS IT: A visible timeline and process summary help explain whether
// round robin shared the CPU and let each real child finish.
// HOW IT WORKS: The metrics holder saves slice and process measurements in vectors,
// then uses simple loops to print elapsed bars and timing totals.

#include "scheduler_metrics.hpp"

#include <iomanip>
#include <iostream>

using namespace std;

namespace safebox {
namespace scheduler {

// Find one process record by PID, returning -1 when it is not registered.
int findProcessIndex(const vector<ProcessMetrics>& processes, pid_t pid) {
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        if (processes[index].pid == pid) {
            return index;
        }
    }
    return -1;
}

// Add a process record with its arrival time relative to scheduler start.
void SchedulerMetrics::addProcess(pid_t pid, long long arrival_ms) {
    ProcessMetrics process{};
    process.pid = pid;
    process.arrival_ms = arrival_ms;
    processes_.push_back(process);
}

// Store one slice and update the process response and CPU-time measurements.
void SchedulerMetrics::addSlice(pid_t pid, long quantum_ms, long long start_ms, long long end_ms,
                                long long cpu_before_ns, long long cpu_after_ns) {
    SliceRecord slice{pid, quantum_ms, start_ms, end_ms, cpu_before_ns, cpu_after_ns};
    slices_.push_back(slice);
    int process_index = findProcessIndex(processes_, pid);
    if (process_index < 0) {
        return;
    }
    ProcessMetrics& process = processes_[process_index];
    if (!process.first_run_ms) {
        process.first_run_ms = start_ms;
        process.first_cpu_ns = cpu_before_ns;
    }
    if (cpu_after_ns >= process.first_cpu_ns) {
        process.total_cpu_ns = cpu_after_ns - process.first_cpu_ns;
    }
    process.slices++;
}

// Record the normal exit time and final process CPU-clock reading.
void SchedulerMetrics::markFinished(pid_t pid, long long finish_ms, int exit_status,
                                    long long final_cpu_ns) {
    int process_index = findProcessIndex(processes_, pid);
    if (process_index < 0) {
        return;
    }
    ProcessMetrics& process = processes_[process_index];
    process.finish_ms = finish_ms;
    process.exit_status = exit_status;
    if (process.first_run_ms && final_cpu_ns >= process.first_cpu_ns) {
        process.total_cpu_ns = final_cpu_ns - process.first_cpu_ns;
    }
}

// Save an abnormal process ending so it remains visible in the summary.
void SchedulerMetrics::markTerminated(pid_t pid, long long finish_ms, const string& reason) {
    int process_index = findProcessIndex(processes_, pid);
    if (process_index >= 0) {
        processes_[process_index].finish_ms = finish_ms;
    }
    termination_reasons_.push_back("P" + to_string(pid) + " " + reason);
}

// Print every time slice as a proportional bar on the elapsed-time line.
void SchedulerMetrics::printTrace() const {
    cout << "\nGantt trace\nPID | start_ms | end_ms | CPU_ms | timeline\n";
    for (int index = 0; index < static_cast<int>(slices_.size()); index++) {
        const SliceRecord& slice = slices_[index];
        long long duration_ms = slice.end_ms - slice.start_ms;
        if (duration_ms < 0) {
            duration_ms = 0;
        }
        int bar_length = static_cast<int>(duration_ms / 10);
        if (bar_length < 1) {
            bar_length = 1;
        }
        if (bar_length > 50) {
            bar_length = 50;
        }
        long long cpu_ms = (slice.cpu_after_ns - slice.cpu_before_ns) / 1000000;
        cout << "P" << slice.pid << " | " << slice.start_ms << " | " << slice.end_ms
             << " | " << cpu_ms << " | [";
        for (int bar_index = 0; bar_index < bar_length; bar_index++) {
            cout << "=";
        }
        cout << "]\n";
    }
}

// Print one process row and return whether it completed normally.
bool printProcessSummary(const ProcessMetrics& process, long long& waiting_total,
                        long long& turnaround_total) {
    long long cpu_ms = process.total_cpu_ns / 1000000;
    long long finish_ms = process.finish_ms.value_or(0);
    long long turnaround_ms = finish_ms - process.arrival_ms;
    long long waiting_ms = turnaround_ms - cpu_ms;
    if (waiting_ms < 0) {
        waiting_ms = 0;
    }
    cout << "P" << process.pid << " | " << process.arrival_ms << " | ";
    if (process.first_run_ms) {
        cout << *process.first_run_ms;
    } else {
        cout << "never";
    }
    cout << " | ";
    if (process.finish_ms) {
        cout << finish_ms;
    } else {
        cout << "running";
    }
    cout << " | " << cpu_ms << " | " << waiting_ms << " | " << turnaround_ms << " | ";
    if (process.first_run_ms) {
        cout << (*process.first_run_ms - process.arrival_ms);
    } else {
        cout << "n/a";
    }
    cout << " | ";
    if (!process.exit_status) {
        cout << "n/a | slices=" << process.slices << "\n";
        return false;
    }
    cout << *process.exit_status << " | slices=" << process.slices << "\n";
    waiting_total += waiting_ms;
    turnaround_total += turnaround_ms;
    return true;
}

// Print timing totals and the number of switches between different processes.
void SchedulerMetrics::printSummary() const {
    cout << "\nSummary\nPID | arrival_ms | first_run_ms | finish_ms | CPU_ms | waiting_ms"
            " | turnaround_ms | response_ms | exit_status\n";
    long long waiting_total = 0;
    long long turnaround_total = 0;
    int completed = 0;
    for (int index = 0; index < static_cast<int>(processes_.size()); index++) {
        if (printProcessSummary(processes_[index], waiting_total, turnaround_total)) {
            completed++;
        }
    }
    int context_switches = 0;
    for (int index = 1; index < static_cast<int>(slices_.size()); index++) {
        if (slices_[index - 1].pid != slices_[index].pid) {
            context_switches++;
        }
    }
    cout << "Context switches: " << context_switches << "\n";
    if (completed > 0) {
        cout << "Average waiting time: " << waiting_total / completed << " ms\n";
        cout << "Average turnaround time: " << turnaround_total / completed << " ms\n";
    }
    for (int index = 0; index < static_cast<int>(termination_reasons_.size()); index++) {
        cout << "Abnormal termination: " << termination_reasons_[index] << "\n";
    }
}

// Give tests read-only access to the recorded slice list.
const vector<SliceRecord>& SchedulerMetrics::slices() const {
    return slices_;
}

}  // namespace scheduler
}  // namespace safebox

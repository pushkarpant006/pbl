// CONCEPT: Scheduler runner
// WHY SAFEBOX NEEDS IT: A scheduling algorithm must be connected to real Linux
// processes so SafeBox can resume, measure, and preempt actual work.
// HOW IT WORKS: The runner resumes one child, watches its process CPU clock until
// the assigned slice is used, then stops it or records its exit status.

#include "scheduler_runner.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

namespace safebox {
namespace scheduler {

enum SliceResult {
    SLICE_PREEMPTED,
    SLICE_EXITED,
    SLICE_SIGNALED,
    SLICE_FAILED
};

struct SliceOutcome {
    SliceResult result;
    int status;
    long long start_ms;
    long long end_ms;
    long long cpu_before_ns;
    long long cpu_after_ns;
    string error;
};

// Return elapsed scheduler time so each trace entry shares one zero point.
long long elapsedMs(chrono::steady_clock::time_point start) {
    chrono::milliseconds elapsed =
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start);
    return elapsed.count();
}

// Read a process CPU clock in nanoseconds for accurate slice accounting.
bool readCpuTimeNs(clockid_t clock_id, long long& cpu_time_ns) {
    timespec time_value{};
    if (clock_gettime(clock_id, &time_value) != 0) {
        return false;
    }
    cpu_time_ns = static_cast<long long>(time_value.tv_sec) * 1000000000LL + time_value.tv_nsec;
    return true;
}

// Ask Linux for the child CPU clock and report errors before scheduling it.
bool getProcessClock(pid_t pid, clockid_t& clock_id) {
    int error_number = clock_getcpuclockid(pid, &clock_id);
    if (error_number == 0) {
        return true;
    }
    cerr << "clock_getcpuclockid(" << pid << "): " << strerror(error_number) << "\n";
    return false;
}

// Check for a child exit or stop, retrying waitpid when a signal interrupts it.
bool collectChildStatus(pid_t pid, int& status, bool wait_for_status) {
    int options = WNOHANG;
    if (wait_for_status) {
        options = WUNTRACED;
    }
    while (true) {
        pid_t waited_pid = waitpid(pid, &status, options);
        if (waited_pid == pid) {
            return true;
        }
        if (waited_pid == 0) {
            return false;
        }
        if (waited_pid == -1 && errno == EINTR) {
            continue;
        }
        if (waited_pid == -1) {
            cerr << "waitpid(" << pid << "): " << strerror(errno) << "\n";
        }
        return false;
    }
}

// Resume a stopped child so the kernel can give it CPU time.
bool resumeProcess(pid_t pid, string& error) {
    if (kill(pid, SIGCONT) == 0) {
        return true;
    }
    error = string("SIGCONT failed: ") + strerror(errno);
    return false;
}

// Watch CPU usage until the requested amount is consumed or the child exits.
bool waitForCpuSlice(pid_t pid, clockid_t cpu_clock, long slice_ms,
                     chrono::steady_clock::time_point start, SliceOutcome& outcome) {
    long long target_ns = static_cast<long long>(slice_ms) * 1000000LL;
    while (outcome.cpu_after_ns - outcome.cpu_before_ns < target_ns) {
        if (!readCpuTimeNs(cpu_clock, outcome.cpu_after_ns)) {
            outcome.error = string("clock_gettime failed: ") + strerror(errno);
            return false;
        }
        if (collectChildStatus(pid, outcome.status, false)) {
            if (WIFEXITED(outcome.status)) {
                outcome.result = SLICE_EXITED;
            } else {
                outcome.result = SLICE_SIGNALED;
            }
            outcome.end_ms = elapsedMs(start);
            return false;
        }
        if (outcome.cpu_after_ns - outcome.cpu_before_ns < target_ns) {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
    return true;
}

// Freeze a child after its slice, then wait for Linux to confirm the stop.
bool pauseAfterSlice(pid_t pid, int& status, string& error) {
    if (kill(pid, SIGSTOP) != 0) {
        error = string("SIGSTOP failed: ") + strerror(errno);
        return false;
    }
    if (!collectChildStatus(pid, status, true)) {
        error = "waitpid did not report a stop or exit";
        return false;
    }
    return true;
}

// Resume a child and save its CPU-clock reading immediately before the slice.
bool beginSlice(pid_t pid, clockid_t& cpu_clock, SliceOutcome& outcome) {
    if (!getProcessClock(pid, cpu_clock)) {
        outcome.error = "could not read process CPU clock";
        return false;
    }
    if (!resumeProcess(pid, outcome.error)) return false;
    if (!readCpuTimeNs(cpu_clock, outcome.cpu_before_ns)) {
        outcome.error = string("clock_gettime failed: ") + strerror(errno);
        return false;
    }
    outcome.cpu_after_ns = outcome.cpu_before_ns;
    return true;
}

// Store the result of waiting for SIGSTOP after the requested slice.
void finishSlice(pid_t pid, clockid_t cpu_clock,
                 chrono::steady_clock::time_point start, SliceOutcome& outcome) {
    if (!pauseAfterSlice(pid, outcome.status, outcome.error)) {
        collectChildStatus(pid, outcome.status, true);
        outcome.result = WIFEXITED(outcome.status) ? SLICE_EXITED : SLICE_SIGNALED;
        outcome.end_ms = elapsedMs(start);
        return;
    }
    if (WIFSTOPPED(outcome.status)) outcome.result = SLICE_PREEMPTED;
    else if (WIFEXITED(outcome.status)) outcome.result = SLICE_EXITED;
    else outcome.result = SLICE_SIGNALED;
    readCpuTimeNs(cpu_clock, outcome.cpu_after_ns);
    outcome.end_ms = elapsedMs(start);
}

// Run one real process slice and save the CPU and wall-clock measurements.
SliceOutcome executeSlice(pid_t pid, long slice_ms, chrono::steady_clock::time_point start) {
    SliceOutcome outcome{SLICE_FAILED, 0, 0, 0, 0, 0, ""};
    clockid_t cpu_clock{};
    if (!beginSlice(pid, cpu_clock, outcome)) return outcome;
    outcome.start_ms = elapsedMs(start);
    if (!waitForCpuSlice(pid, cpu_clock, slice_ms, start, outcome)) return outcome;
    finishSlice(pid, cpu_clock, start, outcome);
    return outcome;
}

// Print and register a process exit before removing it from the ready queue.
void reportProcessResult(pid_t pid, const SliceOutcome& outcome, SchedulerMetrics& metrics,
                         Scheduler& scheduler) {
    if (outcome.result == SLICE_EXITED) {
        int exit_status = WEXITSTATUS(outcome.status);
        cout << "Process " << pid << " finished: exit_status=" << exit_status << "\n";
        metrics.markFinished(pid, outcome.end_ms, exit_status, outcome.cpu_after_ns);
        scheduler.onProcessFinished(pid);
        return;
    }
    if (outcome.result == SLICE_SIGNALED) {
        int signal_number = WTERMSIG(outcome.status);
        string reason = "terminated by signal " + to_string(signal_number);
        cout << "Process " << pid << " " << reason << " (not a normal exit)\n";
        metrics.markTerminated(pid, outcome.end_ms, reason);
        scheduler.onProcessFinished(pid);
        return;
    }
    cerr << "Process " << pid << " scheduler error: " << outcome.error << "\n";
    metrics.markTerminated(pid, outcome.end_ms, outcome.error);
    scheduler.onProcessFinished(pid);
}

// Connect the supplied ready PIDs to the selected scheduling policy.
SchedulerRunner::SchedulerRunner(Scheduler& scheduler, const vector<pid_t>& pids, long quantum_ms)
    : scheduler_(scheduler), pids_(pids), quantum_ms_(quantum_ms) {
}

// Give each ready process one measured slice, then print the collected results.
void SchedulerRunner::run() {
    chrono::steady_clock::time_point scheduler_start = chrono::steady_clock::now();
    for (int index = 0; index < static_cast<int>(pids_.size()); index++) {
        metrics_.addProcess(pids_[index], 0);
    }
    while (scheduler_.hasReadyProcess()) {
        optional<pid_t> next_pid = scheduler_.pickNext();
        if (!next_pid) {
            break;
        }
        pid_t pid = *next_pid;
        long slice_ms = scheduler_.timeSliceMs(pid);
        SliceOutcome outcome = executeSlice(pid, slice_ms, scheduler_start);
        if (outcome.cpu_after_ns >= outcome.cpu_before_ns && outcome.cpu_before_ns > 0) {
            metrics_.addSlice(pid, slice_ms, outcome.start_ms, outcome.end_ms,
                              outcome.cpu_before_ns, outcome.cpu_after_ns);
        }
        if (outcome.result == SLICE_PREEMPTED) {
            scheduler_.onQuantumExpired(pid);
        } else {
            reportProcessResult(pid, outcome, metrics_, scheduler_);
        }
    }
    metrics_.printTrace();
    metrics_.printSummary();
}

// Give tests read-only access to the run's recorded slices.
const vector<SliceRecord>& SchedulerRunner::slices() const {
    return metrics_.slices();
}

}  // namespace scheduler
}  // namespace safebox


#include "findings_builder.hpp"
#include "json_writer.hpp"

#include "../controller/process_control.hpp"
#include "../controller/process_launcher.hpp"
#include "../controller/process_limits.hpp"
#include "../controller/proc_reader.hpp"
#include "../controller/process_table.hpp"
#include "../controller/process_tree.hpp"
#include "../scheduler/round_robin.hpp"
#include "../scheduler/scheduler_metrics.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

using namespace std;
using namespace safebox::controller;
using namespace safebox::scheduler;

namespace {

struct AnalyzerOptions {
    string program_path;
    string program_name;
    long quantum_ms{200};
    long timeout_seconds{15};
};

struct SliceOutcome {
    pid_t pid{0};
    long long start_ms{0};
    long long end_ms{0};
    long cpu_before_ms{0};
    long cpu_after_ms{0};
    bool alive{true};
    bool timed_out{false};
};

long long elapsedMilliseconds(chrono::steady_clock::time_point start) {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - start).count();
}

bool parsePositiveNumber(const string& text, long& value) {
    char* end = nullptr;
    errno = 0;
    value = strtol(text.c_str(), &end, 10);
    return errno == 0 && end != text.c_str() && *end == '\0' && value > 0;
}

bool resolveProgramPath(const string& input, string& output) {
    char resolved_path[4096];
    if (realpath(input.c_str(), resolved_path) == nullptr) return false;
    output = resolved_path;
    return true;
}

bool parseArguments(int argc, char** argv, AnalyzerOptions& options) {
    if (argc < 2 || !resolveProgramPath(argv[1], options.program_path)) return false;
    size_t name_start = options.program_path.find_last_of('/');
    options.program_name = options.program_path.substr(name_start + 1);
    const char* supplied_name = getenv("SAFEBOX_PROGRAM_NAME");
    if (supplied_name != nullptr && supplied_name[0] != '\0') options.program_name = supplied_name;
    for (int index = 2; index < argc; index += 2) {
        if (index + 1 >= argc) return false;
        string option = argv[index];
        long value = 0;
        if (!parsePositiveNumber(argv[index + 1], value)) return false;
        if (option == "--quantum" && value >= 10 && value <= 1000) options.quantum_ms = value;
        else if (option == "--timeout" && value <= 60) options.timeout_seconds = value;
        else return false;
    }
    return true;
}

void emitProcessAdded(const PCB& process, long long time_ms) {
    ostringstream record;
    record << "{\"type\":\"process_added\",\"pid\":" << process.pid
           << ",\"parent_pid\":" << process.parent_pid << ",\"name\":\""
           << safebox::analyzer::escapeJson(process.name) << "\",\"time_ms\":" << time_ms << "}";
    safebox::analyzer::writeJsonLine(record.str());
}

void emitStateChanged(pid_t pid, ProcessState from, ProcessState to, long long time_ms) {
    ostringstream record;
    record << "{\"type\":\"state_changed\",\"pid\":" << pid << ",\"from\":\""
           << processStateName(from) << "\",\"to\":\"" << processStateName(to)
           << "\",\"time_ms\":" << time_ms << "}";
    safebox::analyzer::writeJsonLine(record.str());
}

void emitSlice(const SliceOutcome& slice) {
    ostringstream record;
    record << "{\"type\":\"slice\",\"pid\":" << slice.pid << ",\"start_ms\":"
           << slice.start_ms << ",\"end_ms\":" << slice.end_ms
           << ",\"cpu_ms\":" << slice.cpu_after_ms - slice.cpu_before_ms << "}";
    safebox::analyzer::writeJsonLine(record.str());
}

void emitProcessExited(pid_t pid, const optional<int>& exit_status, long long time_ms) {
    ostringstream record;
    record << "{\"type\":\"process_exited\",\"pid\":" << pid << ",\"exit_status\":";
    if (exit_status) record << *exit_status;
    else record << "null";
    record << ",\"time_ms\":" << time_ms << "}";
    safebox::analyzer::writeJsonLine(record.str());
}

void emitAction(const string& action, pid_t pid, long long time_ms) {
    ostringstream record;
    record << "{\"type\":\"action\",\"action\":\"" << action << "\",\"pid\":"
           << pid << ",\"time_ms\":" << time_ms << "}";
    safebox::analyzer::writeJsonLine(record.str());
}

void sleepTenMilliseconds() {
    timespec delay{0, 10000000};
    nanosleep(&delay, nullptr);
}

bool beginSlice(pid_t pid, ProcessTable& table, string& error, long long time_ms) {
    PCB* process = table.findProcess(pid);
    if (process == nullptr || process->state != ProcessState::READY) {
        error = "scheduler selected a process that was not READY";
        return false;
    }
    if (!table.setState(pid, ProcessState::RUNNING)) {
        error = "illegal READY to RUNNING process transition";
        return false;
    }
    emitStateChanged(pid, ProcessState::READY, ProcessState::RUNNING, time_ms);
    return resumeProcess(pid, error);
}

void waitForQuantum(pid_t pid, long quantum_ms, long timeout_ms,
                    chrono::steady_clock::time_point start, SliceOutcome& outcome,
                    string& error) {
    while (elapsedMilliseconds(start) - outcome.start_ms < quantum_ms) {
        if (elapsedMilliseconds(start) >= timeout_ms) {
            outcome.timed_out = true;
            return;
        }
        ProcessInfo info{};
        if (!readProcessInfo(pid, info, error)) {
            outcome.alive = false;
            return;
        }
        if (info.state == 'Z' || info.state == 'X') {
            outcome.cpu_after_ms = info.cpu_ms;
            outcome.alive = false;
            return;
        }
        outcome.cpu_after_ms = info.cpu_ms;
        sleepTenMilliseconds();
    }
}

bool pauseAfterSlice(SliceOutcome& outcome, string& error) {
    if (!outcome.alive) return true;
    if (!pauseProcess(outcome.pid, error)) {
        ProcessInfo info{};
        string read_error;
        if (!readProcessInfo(outcome.pid, info, read_error)) {
            outcome.alive = false;
            return true;
        }
        return false;
    }
    ProcessInfo info{};
    if (readProcessInfo(outcome.pid, info, error)) {
        outcome.cpu_after_ms = info.cpu_ms;
        if (info.state == 'Z' || info.state == 'X') outcome.alive = false;
    } else {
        outcome.alive = false;
    }
    return true;
}

bool finishSlice(SliceOutcome& outcome, ProcessTable& table, Scheduler& scheduler,
                 SchedulerMetrics& metrics, long quantum_ms, long long time_ms) {
    outcome.end_ms = time_ms;
    metrics.addSlice(outcome.pid, quantum_ms, outcome.start_ms, outcome.end_ms,
                     outcome.cpu_before_ms * 1000000LL, outcome.cpu_after_ms * 1000000LL);
    emitSlice(outcome);
    if (!outcome.alive) return true;
    if (!table.setState(outcome.pid, ProcessState::READY)) return false;
    emitStateChanged(outcome.pid, ProcessState::RUNNING, ProcessState::READY, time_ms);
    scheduler.onQuantumExpired(outcome.pid);
    return true;
}

bool executeSlice(pid_t pid, long quantum_ms, long timeout_ms,
                  chrono::steady_clock::time_point start, ProcessTable& table,
                  Scheduler& scheduler, SchedulerMetrics& metrics, string& error,
                  SliceOutcome& outcome) {
    outcome.pid = pid;
    ProcessInfo before{};
    if (!readProcessInfo(pid, before, error)) return false;
    if (!beginSlice(pid, table, error, elapsedMilliseconds(start))) return false;
    outcome.start_ms = elapsedMilliseconds(start);
    outcome.cpu_before_ms = before.cpu_ms;
    outcome.cpu_after_ms = before.cpu_ms;
    waitForQuantum(pid, quantum_ms, timeout_ms, start, outcome, error);
    if (!pauseAfterSlice(outcome, error)) return false;
    return finishSlice(outcome, table, scheduler, metrics, quantum_ms,
                       elapsedMilliseconds(start));
}

void registerNewProcesses(const ProcessChanges& changes, ProcessTable& table,
                          Scheduler& scheduler, SchedulerMetrics& metrics,
                          long long time_ms) {
    for (int index = 0; index < static_cast<int>(changes.added_pids.size()); index++) {
        PCB* process = table.findProcess(changes.added_pids[index]);
        if (process == nullptr) continue;
        metrics.addProcess(process->pid, time_ms);
        if (process->state == ProcessState::READY) scheduler.addProcess(*process);
        emitProcessAdded(*process, time_ms);
    }
}

int processExitCode(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

optional<int> reapRootIfReady(pid_t root_pid, string& error) {
    int status = 0;
    pid_t result = waitpid(root_pid, &status, WNOHANG);
    if (result == root_pid) return processExitCode(status);
    if (result == -1 && errno != ECHILD) error = string("waitpid failed: ") + strerror(errno);
    return nullopt;
}

bool reportExitedProcesses(const ProcessChanges& changes, ProcessTable& table,
                           Scheduler& scheduler, SchedulerMetrics& metrics,
                           pid_t root_pid, optional<int>& root_status,
                           long long time_ms, string& error) {
    for (int index = 0; index < static_cast<int>(changes.exited_pids.size()); index++) {
        pid_t pid = changes.exited_pids[index];
        PCB* process = table.findProcess(pid);
        if (process == nullptr) continue;
        scheduler.onProcessFinished(pid);
        optional<int> exit_status;
        if (pid == root_pid) {
            root_status = reapRootIfReady(root_pid, error);
            exit_status = root_status;
            process->exit_status = root_status;
        }
        if (exit_status) metrics.markFinished(pid, time_ms, *exit_status, process->cpu_ms * 1000000LL);
        else metrics.markTerminated(pid, time_ms, "process exited");
        emitProcessExited(pid, exit_status, time_ms);
    }
    return error.empty();
}

bool refreshProcesses(ProcessTree& tree, ProcessTable& table, Scheduler& scheduler,
                      SchedulerMetrics& metrics, pid_t root_pid,
                      optional<int>& root_status, long long time_ms) {
    ProcessChanges changes = tree.refreshTable();
    for (int index = 0; index < static_cast<int>(changes.errors.size()); index++) {
        cerr << changes.errors[index] << "\n";
    }
    registerNewProcesses(changes, table, scheduler, metrics, time_ms);
    string error;
    return reportExitedProcesses(changes, table, scheduler, metrics, root_pid,
                                 root_status, time_ms, error);
}

bool hasActiveProcesses(const ProcessTable& table) {
    const vector<PCB>& processes = table.processes();
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        if (processes[index].state != ProcessState::TERMINATED) return true;
    }
    return false;
}

bool terminateApplication(pid_t root_pid, ProcessTable& table, Scheduler& scheduler,
                          SchedulerMetrics& metrics, optional<int>& root_status,
                          chrono::steady_clock::time_point start, string& error) {
    if (!killProcessGroup(root_pid, error)) return false;
    int status = 0;
    pid_t result = waitpid(root_pid, &status, 0);
    if (result == root_pid) root_status = processExitCode(status);
    else if (result == -1 && errno != ECHILD) return false;
    const vector<PCB>& processes = table.processes();
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        pid_t pid = processes[index].pid;
        PCB* process = table.findProcess(pid);
        if (process == nullptr || process->state == ProcessState::TERMINATED) continue;
        ProcessInfo info{};
        string read_error;
        if (readProcessInfo(pid, info, read_error)) process->cpu_ms = info.cpu_ms;
        ProcessState previous = process->state;
        table.setState(pid, ProcessState::TERMINATED);
        emitStateChanged(pid, previous, ProcessState::TERMINATED, elapsedMilliseconds(start));
        scheduler.onProcessFinished(pid);
        optional<int> exit_status;
        if (pid == root_pid) {
            process->exit_status = root_status;
            exit_status = root_status;
        }
        if (exit_status) metrics.markFinished(pid, elapsedMilliseconds(start), *exit_status,
                                               process->cpu_ms * 1000000LL);
        else metrics.markTerminated(pid, elapsedMilliseconds(start), "process group terminated");
        emitProcessExited(pid, exit_status, elapsedMilliseconds(start));
    }
    return true;
}

bool runAnalysis(ProcessTable& table, ProcessTree& tree, Scheduler& scheduler,
                 SchedulerMetrics& metrics, pid_t root_pid, long quantum_ms,
                 long timeout_seconds, chrono::steady_clock::time_point start,
                 optional<int>& root_status, bool& timed_out) {
    long timeout_ms = timeout_seconds * 1000;
    while (hasActiveProcesses(table)) {
        long long now_ms = elapsedMilliseconds(start);
        if (now_ms >= timeout_ms) {
            timed_out = true;
            string error;
            if (!terminateApplication(root_pid, table, scheduler, metrics, root_status, start, error)) {
                cerr << error << "\n";
                return false;
            }
            emitAction("TERMINATE", 0, elapsedMilliseconds(start));
            break;
        }
        optional<pid_t> next_pid = scheduler.pickNext();
        if (!next_pid) {
            sleepTenMilliseconds();
            if (!refreshProcesses(tree, table, scheduler, metrics, root_pid,
                                  root_status, elapsedMilliseconds(start))) return false;
        } else {
            SliceOutcome outcome{};
            string error;
            if (!executeSlice(*next_pid, quantum_ms, timeout_ms, start, table,
                              scheduler, metrics, error, outcome)) {
                cerr << "Could not execute scheduler slice: " << error << "\n";
                return false;
            }
            if (!refreshProcesses(tree, table, scheduler, metrics, root_pid,
                                  root_status, elapsedMilliseconds(start))) return false;
            if (outcome.timed_out) {
                timed_out = true;
                string stop_error;
                if (!terminateApplication(root_pid, table, scheduler, metrics,
                    root_status, start, stop_error)) {
                    cerr << stop_error << "\n";
                    return false;
                }
                emitAction("TERMINATE", 0, elapsedMilliseconds(start));
            }
        }
        if (timed_out) break;
    }
    return true;
}

void appendProcesses(ostringstream& record, const ProcessTable& table) {
    record << "\"processes\":[";
    const vector<PCB>& processes = table.processes();
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        if (index > 0) record << ",";
        const PCB& process = processes[index];
        record << "{\"pid\":" << process.pid << ",\"parent_pid\":" << process.parent_pid
               << ",\"name\":\"" << safebox::analyzer::escapeJson(process.name)
               << "\",\"final_state\":\"" << processStateName(process.state)
               << "\",\"exit_status\":";
        if (process.exit_status) record << *process.exit_status;
        else record << "null";
        record << ",\"cpu_ms\":" << process.cpu_ms << "}";
    }
    record << "]";
}

void appendSlices(ostringstream& record, const SchedulerMetrics& metrics) {
    record << "\"slices\":[";
    const vector<SliceRecord>& slices = metrics.slices();
    for (int index = 0; index < static_cast<int>(slices.size()); index++) {
        if (index > 0) record << ",";
        const SliceRecord& slice = slices[index];
        record << "{\"pid\":" << slice.pid << ",\"start_ms\":" << slice.start_ms
               << ",\"end_ms\":" << slice.end_ms << ",\"cpu_ms\":"
               << (slice.cpu_after_ns - slice.cpu_before_ns) / 1000000 << "}";
    }
    record << "]";
}

void appendMetricList(ostringstream& record, const vector<ProcessMetrics>& processes,
                      const string& name, int metric_kind) {
    record << "\"" << name << "\":[";
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        if (index > 0) record << ",";
        const ProcessMetrics& process = processes[index];
        record << "{\"pid\":" << process.pid << ",\"ms\":";
        if (metric_kind == 2 && !process.first_run_ms) record << "null";
        else if (metric_kind == 0) {
            long long turnaround = process.finish_ms.value_or(0) - process.arrival_ms;
            long long cpu = process.total_cpu_ns / 1000000;
            record << (turnaround > cpu ? turnaround - cpu : 0);
        } else if (metric_kind == 1) {
            record << process.finish_ms.value_or(0) - process.arrival_ms;
        } else {
            record << *process.first_run_ms - process.arrival_ms;
        }
        record << "}";
    }
    record << "]";
}

double calculateAverage(const vector<ProcessMetrics>& processes, int metric_kind) {
    long long total = 0;
    int count = 0;
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        const ProcessMetrics& process = processes[index];
        if (!process.finish_ms || (metric_kind == 2 && !process.first_run_ms)) continue;
        long long turnaround = *process.finish_ms - process.arrival_ms;
        long long cpu = process.total_cpu_ns / 1000000;
        if (metric_kind == 0) total += turnaround > cpu ? turnaround - cpu : 0;
        else if (metric_kind == 1) total += turnaround;
        else total += *process.first_run_ms - process.arrival_ms;
        count++;
    }
    return count == 0 ? 0.0 : static_cast<double>(total) / count;
}

void appendSchedulerMetrics(ostringstream& record, const SchedulerMetrics& metrics) {
    const vector<ProcessMetrics>& processes = metrics.processes();
    record << "\"scheduler_metrics\":{";
    appendMetricList(record, processes, "waiting", 0);
    record << ",";
    appendMetricList(record, processes, "turnaround", 1);
    record << ",";
    appendMetricList(record, processes, "response", 2);
    record << ",\"context_switches\":" << metrics.contextSwitchCount() << ",\"averages\":{";
    record << "\"waiting_ms\":" << calculateAverage(processes, 0)
           << ",\"turnaround_ms\":" << calculateAverage(processes, 1)
           << ",\"response_ms\":" << calculateAverage(processes, 2) << "}}";
}

void appendFindings(ostringstream& record, const safebox::analyzer::FindingsResult& findings) {
    record << "\"findings\":[";
    for (int index = 0; index < static_cast<int>(findings.findings.size()); index++) {
        if (index > 0) record << ",";
        const safebox::analyzer::Finding& finding = findings.findings[index];
        record << "{\"type\":\"" << safebox::analyzer::escapeJson(finding.type)
               << "\",\"severity\":\"" << finding.severity << "\",\"message\":\""
               << safebox::analyzer::escapeJson(finding.message) << "\"}";
    }
    record << "]";
}

void emitSummary(const ProcessTable& table, const SchedulerMetrics& metrics,
                 long long duration_ms, bool timed_out) {
    safebox::analyzer::FindingsResult findings = safebox::analyzer::buildFindings(
        timed_out, static_cast<int>(table.processes().size()));
    long long total_cpu_ms = 0;
    const vector<PCB>& processes = table.processes();
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        total_cpu_ms += processes[index].cpu_ms;
    }
    if (duration_ms <= 0) duration_ms = 1;
    ostringstream record;
    record << fixed << setprecision(2) << "{\"type\":\"summary\",\"verdict\":\""
           << findings.verdict << "\",\"duration_ms\":" << duration_ms
           << ",\"total_cpu_ms\":" << total_cpu_ms
           << ",\"cpu_percent\":" << (100.0 * total_cpu_ms / duration_ms)
           << ",\"process_count\":" << processes.size() << ",";
    appendProcesses(record, table);
    record << ",";
    appendSlices(record, metrics);
    record << ",";
    appendSchedulerMetrics(record, metrics);
    record << ",";
    appendFindings(record, findings);
    record << "}";
    safebox::analyzer::writeJsonLine(record.str());
}

bool registerRoot(pid_t root_pid, const string& name, ProcessTable& table,
                  Scheduler& scheduler, SchedulerMetrics& metrics) {
    PCB root{};
    root.pid = root_pid;
    root.parent_pid = 0;
    root.name = name;
    root.state = ProcessState::READY;
    root.remaining_cpu_ms = static_cast<long>(safebox::controller::PROCESS_CPU_SECONDS * 1000);
    if (!table.addProcess(root)) return false;
    scheduler.addProcess(root);
    metrics.addProcess(root_pid, 0);
    emitProcessAdded(root, 0);
    return true;
}

void removeWorkingFiles(const string& path) {
    DIR* directory = opendir(path.c_str());
    if (directory == nullptr) {
        cerr << "Could not inspect working directory " << path << ": " << strerror(errno) << "\n";
        return;
    }
    dirent* entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if (string(entry->d_name) == "." || string(entry->d_name) == "..") continue;
        string file_path = path + "/" + entry->d_name;
        struct stat file_info{};
        if (lstat(file_path.c_str(), &file_info) != 0) {
            cerr << "Could not inspect working file " << file_path << ": "
                 << strerror(errno) << "\n";
            continue;
        }
        int result = S_ISDIR(file_info.st_mode)
            ? rmdir(file_path.c_str())
            : unlink(file_path.c_str());
        if (result != 0) {
            cerr << "Could not remove working entry " << file_path << ": "
                 << strerror(errno) << "\n";
        }
    }
    closedir(directory);
}

void removeWorkingDirectory(const string& path) {
    removeWorkingFiles(path);
    if (rmdir(path.c_str()) != 0) {
        cerr << "Could not remove working directory " << path << ": " << strerror(errno) << "\n";
    }
}

}
int main(int argc, char** argv) {
    AnalyzerOptions options{};
    if (!parseArguments(argc, argv, options)) {
        cerr << "Usage: safebox_analyze <program> [--quantum 200] [--timeout 15]\n";
        return 2;
    }
    LaunchedProgram launched{};
    string error;
    if (!launchProgram(options.program_path, launched, error)) {
        cerr << error << "\n";
        return 1;
    }
    ProcessTable table;
    RoundRobinScheduler scheduler(options.quantum_ms);
    SchedulerMetrics metrics;
    if (!registerRoot(launched.pid, options.program_name, table, scheduler, metrics)) {
        killProcessGroup(launched.pid, error);
        return 1;
    }
    ProcessTree tree(table, launched.pid);
    chrono::steady_clock::time_point start = chrono::steady_clock::now();
    optional<int> root_status;
    bool timed_out = false;
    bool success = runAnalysis(table, tree, scheduler, metrics, launched.pid,
        options.quantum_ms, options.timeout_seconds, start, root_status,
        timed_out);
    if (!success) {
        killProcessGroup(launched.pid, error);
        waitpid(launched.pid, nullptr, 0);
        removeWorkingDirectory(launched.working_directory);
        return 1;
    }
    emitSummary(table, metrics, elapsedMilliseconds(start), timed_out);
    removeWorkingDirectory(launched.working_directory);
    return 0;
}

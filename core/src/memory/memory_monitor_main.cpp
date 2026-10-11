// CONCEPT: Memory monitoring CLI
// WHY SAFEBOX NEEDS IT: Users need a simple way to watch real process memory and
// optionally pause or kill a process after the detector reports a serious verdict.
// HOW IT WORKS: The CLI launches or attaches to one process, samples procfs in a
// loop, prints memory and page-fault changes, and applies the selected action.

#include "memory_anomaly_detector.hpp"
#include "memory_tracker.hpp"

#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;
using namespace safebox::memory;

enum Action {
    ACTION_NONE,
    ACTION_PAUSE,
    ACTION_KILL
};

struct MonitorOptions {
    pid_t pid;
    bool has_pid;
    bool run_child;
    vector<string> command;
    int interval_ms;
    long limit_kb;
    bool has_limit;
    Action action;
    bool json_mode;
};

struct MonitorSummary {
    long peak_rss_kb;
    string verdict;
    string action;
    long long duration_ms;
    bool has_exit_status;
    int exit_status;
    string error;
};

// Convert an argument to a whole number without accepting trailing text.
bool parseNumber(const string& text, long& number) {
    try {
        size_t parsed = 0;
        number = stol(text, &parsed);
        return parsed == text.size();
    } catch (const exception&) {
        return false;
    }
}

// Read the numeric value following a command-line option.
bool readOptionNumber(int argc, char** argv, int& index, long& value) {
    if (index + 1 >= argc || !parseNumber(argv[index + 1], value)) {
        return false;
    }
    index++;
    return true;
}

// Set the action after checking its three supported names.
bool setAction(const string& name, MonitorOptions& options) {
    if (name == "none") options.action = ACTION_NONE;
    else if (name == "pause") options.action = ACTION_PAUSE;
    else if (name == "kill") options.action = ACTION_KILL;
    else return false;
    return true;
}

// Parse interval, memory-limit, and action options.
bool parseMonitorOption(int argc, char** argv, int& index, MonitorOptions& options) {
    string name = argv[index];
    long value = 0;
    if (name == "--interval") {
        if (!readOptionNumber(argc, argv, index, value) || value <= 0 || value > 60000) return false;
        options.interval_ms = static_cast<int>(value);
        return true;
    }
    if (name == "--limit-mb") {
        if (!readOptionNumber(argc, argv, index, value) || value <= 0 || value > LONG_MAX / 1024) return false;
        options.limit_kb = value * 1024;
        options.has_limit = true;
        return true;
    }
    if (name == "--action" && index + 1 < argc) {
        return setAction(argv[++index], options);
    }
    return false;
}

// Add one command argument when --run has already started a child command.
void addCommandArgument(const string& argument, MonitorOptions& options) {
    options.command.push_back(argument);
}

// Read monitor options and collect the command that should be launched.
bool parseArguments(int argc, char** argv, MonitorOptions& options) {
    options.pid = -1;
    options.has_pid = false;
    options.run_child = false;
    options.interval_ms = 100;
    options.limit_kb = 0;
    options.has_limit = false;
    options.action = ACTION_NONE;
    options.json_mode = false;
    for (int index = 1; index < argc; index++) {
        string argument = argv[index];
        if (argument == "--json") {
            options.json_mode = true;
        } else if (argument == "--run" && index + 1 < argc && !options.has_pid) {
            options.run_child = true;
            options.command.push_back(argv[++index]);
        } else if (argument == "--pid" && !options.run_child && !options.has_pid) {
            long process_id = 0;
            if (!readOptionNumber(argc, argv, index, process_id) || process_id <= 0) return false;
            options.pid = static_cast<pid_t>(process_id);
            options.has_pid = true;
        } else if (argument == "--interval" || argument == "--limit-mb" || argument == "--action") {
            if (!parseMonitorOption(argc, argv, index, options)) return false;
        } else if (options.run_child) {
            addCommandArgument(argument, options);
        } else {
            return false;
        }
    }
    return options.has_pid || !options.command.empty();
}

// Redirect a monitored program so it cannot pollute the JSON output stream.
void redirectChildOutput() {
    int null_file = open("/dev/null", O_WRONLY);
    if (null_file < 0) {
        _exit(126);
    }
    if (dup2(null_file, STDOUT_FILENO) < 0 || dup2(null_file, STDERR_FILENO) < 0) {
        _exit(126);
    }
    close(null_file);
}

// Turn the command string list into argv and execute it as a child process.
void executeCommand(const vector<string>& command, bool json_mode) {
    if (json_mode) {
        redirectChildOutput();
    }
    vector<char*> arguments;
    for (int index = 0; index < static_cast<int>(command.size()); index++) {
        arguments.push_back(const_cast<char*>(command[index].c_str()));
    }
    arguments.push_back(nullptr);
    execvp(arguments[0], arguments.data());
    cerr << "execvp failed for " << command[0] << ": " << strerror(errno) << "\n";
    _exit(127);
}

// Fork the requested program so the monitor can reap its exit status.
pid_t launchCommand(const vector<string>& command, bool json_mode) {
    pid_t child_pid = fork();
    if (child_pid == -1) {
        cerr << "fork failed: " << strerror(errno) << "\n";
        return -1;
    }
    if (child_pid == 0) {
        executeCommand(command, json_mode);
    }
    return child_pid;
}

// Print whether a child returned normally or was ended by a signal.
void printExitStatus(pid_t child_pid, int status, bool json_mode) {
    if (json_mode) return;
    if (WIFEXITED(status)) {
        cout << "Process " << child_pid << " exited normally with status "
             << WEXITSTATUS(status) << " (reaped).\n";
    } else if (WIFSIGNALED(status)) {
        cout << "Process " << child_pid << " was terminated by signal "
             << WTERMSIG(status) << " (reaped).\n";
    }
}

long long elapsedMilliseconds(chrono::steady_clock::time_point start);

// Check for a completed child without blocking and reap it when available.
bool checkChildExit(pid_t child_pid, int& status, bool json_mode,
                    MonitorSummary& summary, chrono::steady_clock::time_point start) {
    pid_t result = waitpid(child_pid, &status, WNOHANG);
    if (result == 0) return false;
    if (result == child_pid) {
        printExitStatus(child_pid, status, json_mode);
        summary.duration_ms = elapsedMilliseconds(start);
        if (WIFEXITED(status)) {
            summary.has_exit_status = true;
            summary.exit_status = WEXITSTATUS(status);
        }
        return true;
    }
    if (result == -1 && errno != EINTR) {
        cerr << "waitpid failed: " << strerror(errno) << "\n";
        summary.error = string("waitpid failed: ") + strerror(errno);
        summary.duration_ms = elapsedMilliseconds(start);
        return true;
    }
    return false;
}

// Remove old samples while preserving the first sample and the latest five seconds.
void trimOldSamples(vector<MemorySample>& samples) {
    if (samples.size() < 3) return;
    chrono::steady_clock::time_point cutoff = samples.back().timestamp - chrono::seconds(5);
    int first_recent = 1;
    while (first_recent < static_cast<int>(samples.size()) &&
           samples[first_recent].timestamp < cutoff) {
        first_recent++;
    }
    int output_index = 1;
    for (int index = first_recent; index < static_cast<int>(samples.size()); index++) {
        samples[output_index] = samples[index];
        output_index++;
    }
    samples.resize(output_index);
}

// Escape control characters so a detector explanation remains valid JSON text.
string escapeJson(const string& value) {
    string escaped;
    for (int index = 0; index < static_cast<int>(value.size()); index++) {
        char character = value[index];
        if (character == '"' || character == '\\') escaped += '\\';
        if (character == '\n') escaped += "\\n";
        else if (character == '\r') escaped += "\\r";
        else if (character == '\t') escaped += "\\t";
        else escaped += character;
    }
    return escaped;
}

// Print one memory measurement and its per-interval fault deltas.
void printSample(const MemorySample& sample, long long elapsed_ms, long minor_delta,
                 long major_delta, const DetectionResult& result, bool json_mode) {
    if (json_mode) {
        cout << "{\"type\":\"sample\",\"pid\":" << sample.pid
             << ",\"elapsed_ms\":" << elapsed_ms << ",\"rss_kb\":" << sample.rss_kb
             << ",\"vsize_kb\":" << sample.vsize_kb
             << ",\"minor_fault_delta\":" << minor_delta
             << ",\"major_fault_delta\":" << major_delta
             << ",\"verdict\":\"" << verdictName(result.verdict)
             << "\",\"reason\":\"" << escapeJson(result.reason) << "\"}\n" << flush;
        return;
    }
    cout << sample.pid << " | " << elapsed_ms << " | " << fixed << setprecision(2)
         << static_cast<double>(sample.rss_kb) / 1024.0 << " | "
         << static_cast<double>(sample.vsize_kb) / 1024.0 << " | "
         << minor_delta << " | " << major_delta << " | "
         << verdictName(result.verdict) << " | " << result.reason << "\n" << flush;
}

// Send the selected operating-system signal and print the action taken.
bool applyAction(pid_t pid, Action action, MemoryVerdict verdict, bool json_mode) {
    int signal_number = action == ACTION_PAUSE ? SIGSTOP : SIGKILL;
    if (kill(pid, signal_number) != 0) {
        cerr << "Could not send action signal: " << strerror(errno) << "\n";
        return false;
    }
    if (!json_mode) {
        string signal_name = action == ACTION_PAUSE ? "SIGSTOP (paused)" : "SIGKILL (killed)";
        cout << "Action: sent " << signal_name << " to PID " << pid << " for "
             << verdictName(verdict) << ".\n" << flush;
        if (action == ACTION_PAUSE) {
            cout << "PID " << pid << " remains paused pending an external user decision.\n" << flush;
        }
    }
    return true;
}

// Wait with repeated nonblocking checks so a paused child can receive a user decision.
void waitForChildExit(pid_t child_pid, bool json_mode, MonitorSummary& summary,
                      chrono::steady_clock::time_point start) {
    int status = 0;
    while (!checkChildExit(child_pid, status, json_mode, summary, start)) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

// Return elapsed time from monitor start for readable sample rows.
long long elapsedMilliseconds(chrono::steady_clock::time_point start) {
    chrono::milliseconds elapsed =
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start);
    return elapsed.count();
}

// Determine whether the current verdict should trigger the configured action.
bool needsAction(MemoryVerdict verdict) {
    return verdict == MemoryVerdict::LIMIT_EXCEEDED ||
           verdict == MemoryVerdict::BOMB_SUSPECTED;
}

// Read one sample, update fault deltas, and print the detector's current verdict.
SampleReadResult processSample(pid_t pid, const MonitorOptions& options,
                               MemoryAnomalyDetector& detector, vector<MemorySample>& samples,
                               MemorySample& previous, bool& has_previous,
                               chrono::steady_clock::time_point start,
                               DetectionResult& detection, MonitorSummary& summary) {
    MemorySample sample{};
    string error;
    SampleReadResult read_result = readSample(pid, sample, error);
    if (read_result != SampleReadResult::SUCCESS) {
        if (read_result == SampleReadResult::ERROR) {
            cerr << "Memory sample failed: " << error << "\n";
        }
        return read_result;
    }
    long minor_delta = has_previous ? sample.minor_faults - previous.minor_faults : 0;
    long major_delta = has_previous ? sample.major_faults - previous.major_faults : 0;
    samples.push_back(sample);
    trimOldSamples(samples);
    optional<long> limit = options.has_limit ? optional<long>(options.limit_kb) : nullopt;
    detection = detector.evaluate(samples, limit);
    long long elapsed_ms = elapsedMilliseconds(start);
    printSample(sample, elapsed_ms, minor_delta, major_delta, detection, options.json_mode);
    if (sample.rss_kb > summary.peak_rss_kb) summary.peak_rss_kb = sample.rss_kb;
    summary.verdict = verdictName(detection.verdict);
    summary.duration_ms = elapsed_ms;
    previous = sample;
    has_previous = true;
    return SampleReadResult::SUCCESS;
}

// Apply one requested action and wait for an external decision if it pauses a child.
int handleAction(pid_t pid, const MonitorOptions& options, MemoryVerdict verdict,
                 bool& action_applied, MonitorSummary& summary,
                 chrono::steady_clock::time_point start) {
    if (action_applied || options.action == ACTION_NONE || !needsAction(verdict)) return 0;
    if (!applyAction(pid, options.action, verdict, options.json_mode)) return 1;
    action_applied = true;
    summary.action = options.action == ACTION_PAUSE ? "paused by SafeBox" : "killed by SafeBox";
    if (options.action == ACTION_PAUSE && options.run_child) {
        if (!options.json_mode) {
            cout << "Waiting for an external user decision before reaping the child.\n" << flush;
        }
        waitForChildExit(pid, options.json_mode, summary, start);
        return 2;
    }
    return 0;
}

// Sample a process until it exits or a user action ends this monitoring run.
void printSummary(const MonitorSummary& summary) {
    cout << "{\"type\":\"summary\",\"verdict\":\"" << summary.verdict
         << "\",\"peak_rss_kb\":" << summary.peak_rss_kb << ",\"exit_status\":";
    if (summary.has_exit_status) cout << summary.exit_status;
    else cout << "null";
    cout << ",\"action\":\"" << summary.action << "\",\"duration_ms\":"
         << summary.duration_ms;
    if (!summary.error.empty()) {
        cout << ",\"error\":\"" << escapeJson(summary.error) << "\"";
    }
    cout << "}\n" << flush;
}

// Sample a process until it exits or a user action ends this monitoring run.
int monitorProcess(const MonitorOptions& options, pid_t pid) {
    MemoryAnomalyDetector detector;
    vector<MemorySample> samples;
    MemorySample previous{};
    bool has_previous = false;
    bool action_applied = false;
    MonitorSummary summary{0, "NORMAL", "none", 0, false, 0, ""};
    chrono::steady_clock::time_point start = chrono::steady_clock::now();
    if (!options.json_mode) {
        cout << "PID | elapsed_ms | RSS_MiB | VSIZE_MiB | minor_fault_delta | "
                "major_fault_delta | verdict | reason\n" << flush;
    }
    while (true) {
        int child_status = 0;
        if (options.run_child && checkChildExit(pid, child_status, options.json_mode,
                                                 summary, start)) {
            if (options.json_mode) printSummary(summary);
            return 0;
        }
        DetectionResult result{};
        SampleReadResult read_result = processSample(pid, options, detector, samples,
                                                      previous, has_previous, start, result,
                                                      summary);
        if (read_result == SampleReadResult::PROCESS_ENDED) {
            if (!options.json_mode) cout << "Process " << pid << " ended (its /proc entry is gone).\n";
            if (options.run_child) waitForChildExit(pid, options.json_mode, summary, start);
            if (options.json_mode) printSummary(summary);
            return 0;
        }
        if (read_result == SampleReadResult::ERROR) {
            if (options.json_mode) {
                summary.error = "Memory sample could not be read.";
                printSummary(summary);
            }
            return 1;
        }
        int action_result = handleAction(pid, options, result.verdict, action_applied,
                                         summary, start);
        if (action_result == 1) {
            if (options.json_mode) {
                summary.error = "Could not apply the selected process action.";
                printSummary(summary);
            }
            return 1;
        }
        if (action_result == 2) {
            if (options.json_mode) printSummary(summary);
            return 0;
        }
        this_thread::sleep_for(chrono::milliseconds(options.interval_ms));
    }
}

// Parse options, launch or select a PID, and begin memory monitoring.
int main(int argc, char** argv) {
    MonitorOptions options{};
    if (!parseArguments(argc, argv, options)) {
        cerr << "usage: safebox_mem (--pid PID | --run COMMAND [ARGS...]) "
                "[--interval MS] [--limit-mb MB] [--action none|pause|kill]\n";
        return 2;
    }
    pid_t pid = options.pid;
    if (options.run_child) {
        pid = launchCommand(options.command, options.json_mode);
        if (pid <= 0) return 1;
    }
    return monitorProcess(options, pid);
}

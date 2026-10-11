// CONCEPT: Scheduler CLI demo
// WHY SAFEBOX NEEDS IT: A command-line run makes the round-robin algorithm visible
// while it controls real child processes instead of simulated work.
// HOW IT WORKS: The demo reads the quantum and CPU targets, starts stopped burners,
// then gives their PIDs to the Round Robin scheduler and its runner.

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include "../controller/process_table.hpp"
#include "round_robin.hpp"
#include "scheduler_runner.hpp"

using namespace std;

namespace {

struct DemoOptions {
    string algorithm;
    long quantum_ms;
    int process_count;
    vector<long> bursts;
    bool json_mode;
};

// Split a comma-separated list into its non-empty text values.
vector<string> splitValues(const string& input) {
    vector<string> values;
    string value;
    stringstream stream(input);
    while (getline(stream, value, ',')) {
        if (!value.empty()) {
            values.push_back(value);
        }
    }
    return values;
}

// Convert burst text into milliseconds for the real CPU-burner processes.
bool parseBursts(const string& input, vector<long>& bursts) {
    vector<string> values = splitValues(input);
    for (int index = 0; index < static_cast<int>(values.size()); index++) {
        try {
            size_t parsed = 0;
            long burst_ms = stol(values[index], &parsed);
            if (parsed != values[index].size() || burst_ms <= 0) {
                return false;
            }
            bursts.push_back(burst_ms);
        } catch (const exception&) {
            return false;
        }
    }
    return true;
}

// Read one named option and update its matching demo value.
bool parseOneOption(const string& argument, const string& value, DemoOptions& options) {
    try {
        if (argument == "--algo") options.algorithm = value;
        else if (argument == "--quantum") options.quantum_ms = stol(value);
        else if (argument == "--procs") options.process_count = stoi(value);
        else if (argument == "--burst") return parseBursts(value, options.bursts);
        else return false;
    } catch (const exception&) {
        return false;
    }
    return true;
}

// Read all supported command-line values and validate the requested policy.
bool parseArguments(int argc, char** argv, DemoOptions& options) {
    options.quantum_ms = 0;
    options.process_count = 0;
    options.json_mode = false;
    for (int index = 1; index < argc; index++) {
        string argument = argv[index];
        if (argument == "--json") {
            options.json_mode = true;
            continue;
        }
        if (index + 1 >= argc) {
            cerr << "Missing value for " << argument << "\n";
            return false;
        }
        string value = argv[++index];
        if (!parseOneOption(argument, value, options)) {
            cerr << "Invalid option or value: " << argument << "\n";
            return false;
        }
    }
    if (options.algorithm != "rr" || options.quantum_ms <= 0 ||
        options.process_count <= 0 ||
        options.process_count != static_cast<int>(options.bursts.size())) {
        cerr << "Use --algo rr with positive quantum and matching --procs/--burst values\n";
        return false;
    }
    return true;
}

// Hide burner output so JSON stdout contains only records written by the demo.
void redirectChildOutput() {
    int null_file = open("/dev/null", O_WRONLY);
    if (null_file < 0) _exit(126);
    if (dup2(null_file, STDOUT_FILENO) < 0 || dup2(null_file, STDERR_FILENO) < 0) {
        _exit(126);
    }
    close(null_file);
}

// Start one child stopped so the scheduler controls its first execution.
bool launchStoppedBurner(long burst_ms, pid_t& child_pid, bool json_mode) {
    child_pid = fork();
    if (child_pid == -1) {
        cerr << "fork failed\n";
        return false;
    }
    if (child_pid == 0) {
        if (json_mode) redirectChildOutput();
        if (raise(SIGSTOP) != 0) {
            _exit(1);
        }
        string burst_text = to_string(burst_ms);
        execl("./cpu_burner", "cpu_burner", burst_text.c_str(), nullptr);
        cerr << "exec failed for cpu_burner\n";
        _exit(1);
    }
    int status = 0;
    if (waitpid(child_pid, &status, WUNTRACED) != child_pid || !WIFSTOPPED(status)) {
        cerr << "parent could not confirm the stopped child\n";
        return false;
    }
    return true;
}

// Launch all CPU burners and record their PIDs in the same order as burst values.
bool launchBurners(const vector<long>& bursts, vector<pid_t>& process_ids, bool json_mode) {
    for (int index = 0; index < static_cast<int>(bursts.size()); index++) {
        pid_t child_pid = 0;
        if (!launchStoppedBurner(bursts[index], child_pid, json_mode)) {
            return false;
        }
        process_ids.push_back(child_pid);
    }
    return true;
}

// Add one shared PCB for each real child to the scheduler's ready list.
void addProcesses(safebox::scheduler::RoundRobinScheduler& scheduler,
                  const vector<pid_t>& process_ids, const vector<long>& bursts) {
    for (int index = 0; index < static_cast<int>(process_ids.size()); index++) {
        safebox::controller::PCB process{};
        process.pid = process_ids[index];
        process.state = safebox::controller::ProcessState::READY;
        process.total_cpu_ms = bursts[index];
        process.remaining_cpu_ms = bursts[index];
        scheduler.addProcess(process);
    }
}

// Read normal-exit statuses from the runner's captured human-readable report.
map<pid_t, int> parseExitStatuses(const string& output) {
    map<pid_t, int> statuses;
    stringstream output_stream(output);
    string line;
    while (getline(output_stream, line)) {
        size_t process_start = line.find("Process ");
        size_t finished_position = line.find(" finished: exit_status=");
        if (process_start == string::npos || finished_position == string::npos) continue;
        try {
            string pid_text = line.substr(process_start + 8, finished_position - process_start - 8);
            size_t status_start =
                finished_position + string(" finished: exit_status=").size();
            statuses[static_cast<pid_t>(stol(pid_text))] =
                stoi(line.substr(status_start));
        } catch (const exception&) {
            continue;
        }
    }
    return statuses;
}

// Print one measured slice as a JSON Lines record.
void printJsonSlice(const safebox::scheduler::SliceRecord& slice) {
    long long cpu_ms = (slice.cpu_after_ns - slice.cpu_before_ns) / 1000000;
    cout << "{\"type\":\"slice\",\"pid\":" << slice.pid
         << ",\"start_ms\":" << slice.start_ms << ",\"end_ms\":" << slice.end_ms
         << ",\"cpu_ms\":" << cpu_ms << "}\n";
}

struct JsonProcessTotals {
    bool has_slice;
    long long first_run_ms;
    long long finish_ms;
    long long cpu_ms;
    int slices;
};

// Add every measured slice belonging to one process.
JsonProcessTotals collectProcessTotals(pid_t pid,
                                       const vector<safebox::scheduler::SliceRecord>& slices) {
    JsonProcessTotals totals{false, 0, 0, 0, 0};
    for (int index = 0; index < static_cast<int>(slices.size()); index++) {
        if (slices[index].pid != pid) continue;
        if (!totals.has_slice) totals.first_run_ms = slices[index].start_ms;
        totals.has_slice = true;
        totals.finish_ms = slices[index].end_ms;
        totals.cpu_ms +=
            (slices[index].cpu_after_ns - slices[index].cpu_before_ns) / 1000000;
        totals.slices++;
    }
    return totals;
}

// Print calculated timing and CPU totals for one process in the final JSON record.
void printJsonProcess(pid_t pid, const vector<safebox::scheduler::SliceRecord>& slices,
                      const map<pid_t, int>& exit_statuses);

// Print per-process scheduling totals and the total number of context switches.
void printJsonSummary(const vector<pid_t>& process_ids,
                      const vector<safebox::scheduler::SliceRecord>& slices,
                      const map<pid_t, int>& exit_statuses) {
    int context_switches = 0;
    for (int index = 1; index < static_cast<int>(slices.size()); index++) {
        if (slices[index - 1].pid != slices[index].pid) context_switches++;
    }
    cout << "{\"type\":\"summary\",\"processes\":[";
    for (int process_index = 0; process_index < static_cast<int>(process_ids.size());
         process_index++) {
        if (process_index > 0) cout << ",";
        printJsonProcess(process_ids[process_index], slices, exit_statuses);
    }
    cout << "],\"context_switches\":" << context_switches
         << ",\"slice_count\":" << slices.size() << "}\n" << flush;
}

// Print calculated timing and CPU totals for one process in the final JSON record.
void printJsonProcess(pid_t pid, const vector<safebox::scheduler::SliceRecord>& slices,
                      const map<pid_t, int>& exit_statuses) {
    JsonProcessTotals totals = collectProcessTotals(pid, slices);
    long long waiting_ms = totals.finish_ms - totals.cpu_ms;
    if (waiting_ms < 0) waiting_ms = 0;
    cout << "{\"pid\":" << pid << ",\"first_run_ms\":";
    if (totals.has_slice) cout << totals.first_run_ms;
    else cout << "null";
    cout << ",\"finish_ms\":";
    if (totals.has_slice) cout << totals.finish_ms;
    else cout << "null";
    cout << ",\"cpu_ms\":" << totals.cpu_ms << ",\"waiting_ms\":" << waiting_ms
         << ",\"turnaround_ms\":" << totals.finish_ms << ",\"response_ms\":";
    if (totals.has_slice) cout << totals.first_run_ms;
    else cout << "null";
    cout << ",\"slices\":" << totals.slices << ",\"exit_status\":";
    map<pid_t, int>::const_iterator status = exit_statuses.find(pid);
    if (status == exit_statuses.end()) cout << "null";
    else cout << status->second;
    cout << "}";
}

// Run the scheduler while keeping its text report out of JSON stdout.
void runJsonDemo(safebox::scheduler::SchedulerRunner& runner,
                 const vector<pid_t>& process_ids) {
    ostringstream captured_output;
    streambuf* original_output = cout.rdbuf(captured_output.rdbuf());
    runner.run();
    cout.rdbuf(original_output);
    map<pid_t, int> exit_statuses = parseExitStatuses(captured_output.str());
    const vector<safebox::scheduler::SliceRecord>& slices = runner.slices();
    for (int index = 0; index < static_cast<int>(slices.size()); index++) {
        printJsonSlice(slices[index]);
    }
    printJsonSummary(process_ids, slices, exit_statuses);
}

}  // namespace

// Parse the demo options and connect real CPU burners to the Round Robin runner.
int main(int argc, char** argv) {
    DemoOptions options{};
    if (!parseArguments(argc, argv, options)) {
        return 1;
    }
    vector<pid_t> process_ids;
    if (!launchBurners(options.bursts, process_ids, options.json_mode)) {
        return 1;
    }
    safebox::scheduler::RoundRobinScheduler scheduler(options.quantum_ms);
    addProcesses(scheduler, process_ids, options.bursts);
    safebox::scheduler::SchedulerRunner runner(scheduler, process_ids, options.quantum_ms);
    if (options.json_mode) runJsonDemo(runner, process_ids);
    else runner.run();
    return 0;
}

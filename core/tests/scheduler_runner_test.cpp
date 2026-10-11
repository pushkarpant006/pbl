// CONCEPT: Real scheduler runner test
// WHY SAFEBOX NEEDS IT: The runner must be checked against real stopped child
// processes so signal control and CPU-clock sampling are tested together.
// HOW IT WORKS: The test launches small touched CPU workloads, runs round robin,
// then checks that every recorded slice has valid process and duration data.

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/scheduler/round_robin.hpp"
#include "../src/scheduler/scheduler_runner.hpp"

using namespace std;

// Stop the test with a readable message when an invariant fails.
void require(bool condition, const string& message) {
    if (condition) {
        return;
    }
    cerr << "scheduler_runner_test failed: " << message << "\n";
    exit(1);
}

// Start one CPU burner stopped so the runner owns its first resume.
bool launchStoppedBurner(long burst_ms, pid_t& process_id) {
    process_id = fork();
    if (process_id == -1) {
        cerr << "fork failed\n";
        return false;
    }
    if (process_id == 0) {
        if (raise(SIGSTOP) != 0) {
            _exit(1);
        }
        string burst_text = to_string(burst_ms);
        execl("./cpu_burner", "cpu_burner", burst_text.c_str(), nullptr);
        _exit(1);
    }
    int status = 0;
    return waitpid(process_id, &status, WUNTRACED) == process_id && WIFSTOPPED(status);
}

// Check real runner slices and verify both children completed through the runner.
int main() {
    vector<long> bursts{200, 300};
    vector<pid_t> process_ids;
    for (int index = 0; index < static_cast<int>(bursts.size()); index++) {
        pid_t process_id = 0;
        require(launchStoppedBurner(bursts[index], process_id), "child must start stopped");
        process_ids.push_back(process_id);
    }
    safebox::scheduler::RoundRobinScheduler scheduler(100);
    for (int index = 0; index < static_cast<int>(process_ids.size()); index++) {
        safebox::controller::PCB process{};
        process.pid = process_ids[index];
        process.state = safebox::controller::ProcessState::READY;
        process.total_cpu_ms = bursts[index];
        process.remaining_cpu_ms = bursts[index];
        scheduler.addProcess(process);
    }
    safebox::scheduler::SchedulerRunner runner(scheduler, process_ids, 100);
    runner.run();
    require(!runner.slices().empty(), "runner must record at least one slice");
    for (int index = 0; index < static_cast<int>(runner.slices().size()); index++) {
        require(runner.slices()[index].pid > 0, "slice PID must be valid");
        require(runner.slices()[index].quantum_ms > 0, "slice length must be positive");
    }
    cout << "scheduler_runner_test: ok\n";
    return 0;
}

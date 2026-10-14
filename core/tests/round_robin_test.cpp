
#include <cstdlib>
#include <iostream>
#include <string>

#include "../src/scheduler/round_robin.hpp"

using namespace std;

void require(bool condition, const string& message) {
    if (condition) {
        return;
    }
    cerr << "round_robin_test failed: " << message << "\n";
    exit(1);
}

safebox::controller::PCB makeProcess(pid_t pid, long cpu_ms) {
    safebox::controller::PCB process{};
    process.pid = pid;
    process.total_cpu_ms = cpu_ms;
    process.remaining_cpu_ms = cpu_ms;
    return process;
}

int main() {
    safebox::scheduler::RoundRobinScheduler scheduler(100);
    scheduler.addProcess(makeProcess(101, 500));
    scheduler.addProcess(makeProcess(202, 400));
    scheduler.addProcess(makeProcess(303, 300));
    require(scheduler.hasReadyProcess(), "initial processes must be ready");
    require(scheduler.pickNext().value() == 101, "first process must run first");
    scheduler.onQuantumExpired(101);
    require(scheduler.pickNext().value() == 202, "second process must run second");
    scheduler.onQuantumExpired(202);
    require(scheduler.pickNext().value() == 303, "third process must run third");
    scheduler.onProcessFinished(303);
    require(scheduler.pickNext().value() == 101, "unfinished first process must rotate");
    scheduler.onQuantumExpired(101);
    require(scheduler.pickNext().value() == 202, "unfinished second process must rotate");
    scheduler.onQuantumExpired(202);
    require(scheduler.timeSliceMs(101) == 100, "remaining time is bounded by quantum");
    scheduler.onProcessFinished(101);
    scheduler.onProcessFinished(202);
    require(!scheduler.hasReadyProcess(), "finished processes must leave the queue");
    cout << "round_robin_test: ok\n";
    return 0;
}

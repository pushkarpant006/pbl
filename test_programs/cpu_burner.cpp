// CONCEPT: CPU burner workload
// WHY SAFEBOX NEEDS IT: Real busy CPU work lets the scheduler demonstrate Linux
// preemption rather than relying on a simulated process timeline.
// HOW IT WORKS: The process reads its own CPU clock and loops until it has used the
// requested CPU milliseconds, printing a message before and after the work.

#include <cstdlib>
#include <iostream>
#include <string>
#include <time.h>
#include <unistd.h>

using namespace std;

// Read this process's CPU clock as milliseconds.
long readProcessCpuMs() {
    timespec cpu_time{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_time) != 0) {
        return -1;
    }
    return static_cast<long>(cpu_time.tv_sec * 1000L + cpu_time.tv_nsec / 1000000L);
}

// Validate the target argument and convert it to a positive CPU duration.
bool parseTarget(int argc, char** argv, long& target_ms) {
    if (argc != 2) {
        cerr << "usage: cpu_burner <target_ms>\n";
        return false;
    }
    try {
        size_t parsed = 0;
        target_ms = stol(argv[1], &parsed);
        return parsed == string(argv[1]).size() && target_ms > 0;
    } catch (const exception&) {
        cerr << "target_ms must be a positive integer\n";
        return false;
    }
}

// Keep the CPU busy until the requested process CPU time has elapsed.
bool burnCpu(long target_ms) {
    long start_ms = readProcessCpuMs();
    if (start_ms < 0) {
        cerr << "clock_gettime failed for process CPU clock\n";
        return false;
    }
    while (true) {
        long current_ms = readProcessCpuMs();
        if (current_ms < 0) {
            cerr << "clock_gettime failed for process CPU clock\n";
            return false;
        }
        if (current_ms - start_ms >= target_ms) {
            cout << "cpu_burner pid=" << getpid() << " finished used_cpu_ms="
                 << current_ms - start_ms << "\n" << flush;
            return true;
        }
    }
}

// Print workload progress and run the requested CPU busy loop.
int main(int argc, char** argv) {
    long target_ms = 0;
    if (!parseTarget(argc, argv, target_ms)) {
        return 1;
    }
    cout << "cpu_burner pid=" << getpid() << " starting target_ms=" << target_ms
         << "\n" << flush;
    return burnCpu(target_ms) ? 0 : 1;
}

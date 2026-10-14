
#include <cstdint>
#include <iostream>
#include <time.h>
#include <unistd.h>

namespace {

constexpr long long CPU_TARGET_MS = 2500;

long long readProcessCpuMilliseconds() {
    timespec cpu_time{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_time) != 0) return -1;
    return static_cast<long long>(cpu_time.tv_sec) * 1000 +
           cpu_time.tv_nsec / 1000000;
}

bool useBoundedCpuTime() {
    long long start_ms = readProcessCpuMilliseconds();
    if (start_ms < 0) {
        std::cerr << "Could not read process CPU time.\n";
        return false;
    }
    volatile std::uint64_t work = 1;
    while (true) {
        work = work * 1664525 + 1013904223;
        long long current_ms = readProcessCpuMilliseconds();
        if (current_ms < 0) {
            std::cerr << "Could not read process CPU time.\n";
            return false;
        }
        if (current_ms - start_ms >= CPU_TARGET_MS) {
            std::cout << "CPU test finished after "
                      << current_ms - start_ms << " CPU ms; checksum="
                      << work << "\n";
            return true;
        }
    }
}

}
int main() {
    std::cout << "Starting bounded CPU test (maximum 2500 CPU ms), PID "
              << getpid() << "\n";
    return useBoundedCpuTime() ? 0 : 1;
}

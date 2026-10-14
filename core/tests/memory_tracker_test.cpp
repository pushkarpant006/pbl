
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

#include "../src/memory/memory_tracker.hpp"

using namespace std;

void require(bool condition, const string& message) {
    if (condition) {
        return;
    }
    cerr << "memory_tracker_test failed: " << message << "\n";
    exit(1);
}

void checkStatParsing(const string& comm) {
    string line = "123 (" + comm + ") R 1 2 3 4 5 6 111 8 222";
    optional<safebox::memory::ProcessStatFields> fields =
        safebox::memory::parseStatLine(line);
    require(fields.has_value(), "stat parser accepts a wrapped comm field");
    require(fields->minor_faults == 111, "minor fault field is parsed");
    require(fields->major_faults == 222, "major fault field is parsed");
}

void checkOwnProcessSample() {
    safebox::memory::MemorySample sample{};
    string error;
    safebox::memory::SampleReadResult result =
        safebox::memory::readSample(getpid(), sample, error);
    require(result == safebox::memory::SampleReadResult::SUCCESS, "own procfs sample succeeds");
    require(error.empty(), "successful sample has no error message");
    require(sample.pid == getpid(), "sample retains the requested PID");
    require(sample.timestamp <= chrono::steady_clock::now(), "timestamp is from the past");
    require(sample.rss_kb >= 0 && sample.vsize_kb >= sample.rss_kb,
            "memory counters are nonnegative");
}

void checkMissingProcess() {
    safebox::memory::MemorySample sample{};
    string error;
    safebox::memory::SampleReadResult result =
        safebox::memory::readSample(2147483647, sample, error);
    require(result == safebox::memory::SampleReadResult::PROCESS_ENDED,
            "missing process is reported as ended");
}

int main() {
    checkStatParsing("(a b) c");
    checkStatParsing("((x))");
    checkOwnProcessSample();
    checkMissingProcess();
    cout << "memory_tracker_test: ok\n";
    return 0;
}

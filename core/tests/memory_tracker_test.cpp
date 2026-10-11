// CONCEPT: Procfs memory tracker test
// WHY SAFEBOX NEEDS IT: Procfs parsing must handle kernel process names accurately
// and return real counters for a process that is currently running.
// HOW IT WORKS: The test checks two difficult comm strings, reads its own procfs
// files, and confirms that an unavailable PID is reported as an ended process.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

#include "../src/memory/memory_tracker.hpp"

using namespace std;

// Stop with a useful message if an expected procfs property is missing.
void require(bool condition, const string& message) {
    if (condition) {
        return;
    }
    cerr << "memory_tracker_test failed: " << message << "\n";
    exit(1);
}

// Check fault values after a comm field that contains parentheses or spaces.
void checkStatParsing(const string& comm) {
    string line = "123 (" + comm + ") R 1 2 3 4 5 6 111 8 222";
    optional<safebox::memory::ProcessStatFields> fields =
        safebox::memory::parseStatLine(line);
    require(fields.has_value(), "stat parser accepts a wrapped comm field");
    require(fields->minor_faults == 111, "minor fault field is parsed");
    require(fields->major_faults == 222, "major fault field is parsed");
}

// Read and validate the current test process's actual Linux memory counters.
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

// Confirm an absent procfs directory is a process-ended result, not a parse error.
void checkMissingProcess() {
    safebox::memory::MemorySample sample{};
    string error;
    safebox::memory::SampleReadResult result =
        safebox::memory::readSample(2147483647, sample, error);
    require(result == safebox::memory::SampleReadResult::PROCESS_ENDED,
            "missing process is reported as ended");
}

// Run parser and real-procfs checks for the tracker.
int main() {
    checkStatParsing("(a b) c");
    checkStatParsing("((x))");
    checkOwnProcessSample();
    checkMissingProcess();
    cout << "memory_tracker_test: ok\n";
    return 0;
}

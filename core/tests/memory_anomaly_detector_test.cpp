// CONCEPT: Memory detector unit test
// WHY SAFEBOX NEEDS IT: Synthetic data makes detector thresholds repeatable without
// relying on procfs, machine load, or real memory-allocation timing.
// HOW IT WORKS: The test creates timestamped samples and checks warm-up, normal,
// bomb, leak, and caller-limit verdicts along with their explanations.

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "../src/memory/memory_anomaly_detector.hpp"

using namespace std;

typedef safebox::memory::MemorySample Sample;
typedef chrono::steady_clock Clock;

// Stop the test with a readable explanation when a condition fails.
void require(bool condition, const string& message) {
    if (condition) {
        return;
    }
    cerr << "memory_anomaly_detector_test failed: " << message << "\n";
    exit(1);
}

// Build one synthetic process sample at a known elapsed second.
Sample makeSample(int seconds, long rss_mb) {
    Sample sample{};
    sample.timestamp = Clock::time_point{} + chrono::seconds(seconds);
    sample.pid = 77;
    sample.rss_kb = rss_mb * 1024;
    sample.vsize_kb = rss_mb * 2048;
    sample.minor_faults = seconds * 10;
    sample.major_faults = seconds;
    return sample;
}

// Confirm startup warm-up does not report a short growth history as abnormal.
void checkWarmupAndNormal() {
    safebox::memory::MemoryAnomalyDetector detector;
    vector<Sample> samples;
    samples.push_back(makeSample(0, 20));
    samples.push_back(makeSample(1, 21));
    samples.push_back(makeSample(2, 21));
    samples.push_back(makeSample(3, 21));
    samples.push_back(makeSample(4, 21));
    safebox::memory::DetectionResult result = detector.evaluate(samples);
    require(result.verdict == safebox::memory::MemoryVerdict::NORMAL, "stable usage stays normal");
    require(result.reason.find("no configured") != string::npos, "normal result explains its verdict");
}

// Confirm a high short-window growth rate and a caller limit have different verdicts.
void checkBombAndLimit() {
    safebox::memory::MemoryAnomalyDetector detector;
    vector<Sample> samples;
    samples.push_back(makeSample(0, 10));
    samples.push_back(makeSample(1, 20));
    samples.push_back(makeSample(2, 30));
    samples.push_back(makeSample(3, 40));
    samples.push_back(makeSample(4, 50));
    samples.push_back(makeSample(5, 160));
    Sample spike = makeSample(5, 250);
    spike.timestamp = Clock::time_point{} + chrono::milliseconds(5200);
    samples.push_back(spike);
    safebox::memory::DetectionResult bomb = detector.evaluate(samples);
    require(bomb.verdict == safebox::memory::MemoryVerdict::BOMB_SUSPECTED, "rapid growth is a bomb");
    require(bomb.reason.find("MiB/s") != string::npos, "bomb reason includes measured rate");
    safebox::memory::DetectionResult limit = detector.evaluate(vector<Sample>(1, makeSample(0, 12)),
                                                                10 * 1024);
    require(limit.verdict == safebox::memory::MemoryVerdict::LIMIT_EXCEEDED, "caller limit takes effect");
    require(limit.reason.find("limit") != string::npos, "limit reason includes the configured boundary");
}

// Confirm steady positive growth crosses the long-window leak threshold.
void checkLeak() {
    safebox::memory::MemoryAnomalyDetector detector;
    vector<Sample> samples;
    samples.push_back(makeSample(0, 20));
    samples.push_back(makeSample(1, 21));
    samples.push_back(makeSample(2, 22));
    samples.push_back(makeSample(3, 23));
    samples.push_back(makeSample(4, 24));
    samples.push_back(makeSample(5, 25));
    safebox::memory::DetectionResult result = detector.evaluate(samples);
    require(result.verdict == safebox::memory::MemoryVerdict::LEAK_SUSPECTED, "steady growth is a leak");
    require(result.reason.find("Sustained") != string::npos, "leak reason names sustained growth");
}

// Confirm the leak verdict uses the most recent full configured window.
void checkRecentLeakWindow() {
    safebox::memory::MemoryAnomalyDetector detector;
    vector<Sample> samples;
    for (int second = 0; second <= 5; second++) {
        samples.push_back(makeSample(second, 10));
    }
    samples.push_back(makeSample(6, 11));
    samples.push_back(makeSample(7, 12));
    samples.push_back(makeSample(8, 13));
    safebox::memory::DetectionResult result = detector.evaluate(samples);
    require(result.verdict == safebox::memory::MemoryVerdict::LEAK_SUSPECTED,
            "recent positive growth must still be found");
}

// Run each detector behavior check and report a single test result.
int main() {
    checkWarmupAndNormal();
    checkBombAndLimit();
    checkLeak();
    checkRecentLeakWindow();
    cout << "memory_anomaly_detector_test: ok\n";
    return 0;
}

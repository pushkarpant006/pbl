
#include "memory_anomaly_detector.hpp"

#include <sstream>

using namespace std;

namespace safebox {
namespace memory {
namespace {

double rssMegabytes(const MemorySample& sample) {
    return static_cast<double>(sample.rss_kb) / 1024.0;
}

double timeBetweenSeconds(const MemorySample& newer, const MemorySample& older) {
    chrono::duration<double> elapsed = newer.timestamp - older.timestamp;
    return elapsed.count();
}

int findLeakWindowStart(const vector<MemorySample>& samples, double required_seconds) {
    int start_index = -1;
    double closest_duration = 0.0;
    for (int index = 0; index < static_cast<int>(samples.size()) - 1; index++) {
        double duration = timeBetweenSeconds(samples.back(), samples[index]);
        if (duration >= required_seconds &&
            (start_index == -1 || duration < closest_duration)) {
            start_index = index;
            closest_duration = duration;
        }
    }
    return start_index;
}

int countPositiveSteps(const vector<MemorySample>& samples, int start_index) {
    int positive_steps = 0;
    for (int index = start_index + 1; index < static_cast<int>(samples.size()); index++) {
        if (samples[index].rss_kb > samples[index - 1].rss_kb) {
            positive_steps++;
        }
    }
    return positive_steps;
}

DetectionResult normalResult(const string& reason) {
    DetectionResult result{};
    result.verdict = MemoryVerdict::NORMAL;
    result.reason = reason;
    return result;
}

bool checkLimit(const MemorySample& sample, optional<long> limit_kb,
                DetectionResult& result) {
    if (!limit_kb || sample.rss_kb < *limit_kb) {
        return false;
    }
    ostringstream reason;
    reason << "RSS is " << rssMegabytes(sample) << " MiB, at or above the caller limit of "
           << static_cast<double>(*limit_kb) / 1024.0 << " MiB.";
    result.verdict = MemoryVerdict::LIMIT_EXCEEDED;
    result.reason = reason.str();
    return true;
}

bool checkBomb(const vector<MemorySample>& samples, const DetectorConfig& config,
               DetectionResult& result) {
    int start_index = -1;
    double longest_seconds = 0.0;
    double minimum_seconds = chrono::duration<double>(config.bomb_min_window).count();
    double maximum_seconds = chrono::duration<double>(config.bomb_window).count();
    for (int index = 0; index < static_cast<int>(samples.size()) - 1; index++) {
        double duration = timeBetweenSeconds(samples.back(), samples[index]);
        if (duration >= minimum_seconds && duration <= maximum_seconds &&
            duration > longest_seconds) {
            start_index = index;
            longest_seconds = duration;
        }
    }
    if (start_index == -1) {
        return false;
    }
    double growth_mb = rssMegabytes(samples.back()) - rssMegabytes(samples[start_index]);
    double rate = growth_mb / longest_seconds;
    if (rate < config.bomb_rate_mb_per_second) {
        return false;
    }
    ostringstream reason;
    reason << "RSS grew " << growth_mb << " MiB in " << longest_seconds
           << " s (" << rate << " MiB/s), meeting the bomb threshold of "
           << config.bomb_rate_mb_per_second << " MiB/s.";
    result.verdict = MemoryVerdict::BOMB_SUSPECTED;
    result.reason = reason.str();
    return true;
}

bool checkLeak(const vector<MemorySample>& samples, const DetectorConfig& config,
               DetectionResult& result) {
    double required_seconds = chrono::duration<double>(config.leak_window).count();
    int start_index = findLeakWindowStart(samples, required_seconds);
    if (start_index < 0 ||
        static_cast<int>(samples.size()) - start_index < static_cast<int>(config.leak_min_samples)) {
        return false;
    }
    double duration = timeBetweenSeconds(samples.back(), samples[start_index]);
    double growth_mb = rssMegabytes(samples.back()) - rssMegabytes(samples[start_index]);
    double rate = growth_mb / duration;
    int steps = static_cast<int>(samples.size()) - start_index - 1;
    int positive_steps = countPositiveSteps(samples, start_index);
    double positive_fraction = static_cast<double>(positive_steps) / steps;
    if (growth_mb <= 0.0 || rate < config.leak_rate_mb_per_second ||
        positive_fraction < config.leak_positive_step_fraction) {
        return false;
    }
    ostringstream reason;
    reason << "Sustained RSS growth: " << growth_mb << " MiB in " << duration
           << " s (" << rate << " MiB/s) with " << positive_fraction * 100.0
           << "% positive samples; thresholds are " << config.leak_rate_mb_per_second
           << " MiB/s and " << config.leak_positive_step_fraction * 100.0
           << "% positive samples.";
    result.verdict = MemoryVerdict::LEAK_SUSPECTED;
    result.reason = reason.str();
    return true;
}

}
MemoryAnomalyDetector::MemoryAnomalyDetector(DetectorConfig config) {
    config_ = config;
}

DetectionResult MemoryAnomalyDetector::evaluate(const vector<MemorySample>& input,
                                                optional<long> limit_kb) const {
    if (input.empty()) {
        return normalResult("No memory samples are available yet.");
    }
    vector<MemorySample> samples = input;
    for (int index = 0; index < static_cast<int>(samples.size()); index++) {
        for (int next_index = index + 1; next_index < static_cast<int>(samples.size()); next_index++) {
            if (samples[next_index].timestamp < samples[index].timestamp) {
                MemorySample temporary = samples[index];
                samples[index] = samples[next_index];
                samples[next_index] = temporary;
            }
        }
    }
    DetectionResult result{};
    if (checkLimit(samples.back(), limit_kb, result)) {
        return result;
    }
    double observed_seconds = timeBetweenSeconds(samples.back(), samples.front());
    double warmup_seconds = chrono::duration<double>(config_.warmup_period).count();
    if (observed_seconds < warmup_seconds) {
        ostringstream reason;
        reason << "Startup warm-up: observed " << observed_seconds << " s of "
               << warmup_seconds << " s; RSS is " << rssMegabytes(samples.back()) << " MiB.";
        return normalResult(reason.str());
    }
    if (checkBomb(samples, config_, result) || checkLeak(samples, config_, result)) {
        return result;
    }
    ostringstream reason;
    reason << "RSS is " << rssMegabytes(samples.back())
           << " MiB; no configured growth-rate threshold was met.";
    return normalResult(reason.str());
}

const char* verdictName(MemoryVerdict verdict) {
    if (verdict == MemoryVerdict::NORMAL) return "NORMAL";
    if (verdict == MemoryVerdict::LEAK_SUSPECTED) return "LEAK_SUSPECTED";
    if (verdict == MemoryVerdict::BOMB_SUSPECTED) return "BOMB_SUSPECTED";
    if (verdict == MemoryVerdict::LIMIT_EXCEEDED) return "LIMIT_EXCEEDED";
    return "UNKNOWN";
}

}}
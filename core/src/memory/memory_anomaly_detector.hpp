// CONCEPT: Memory growth-rate detection
// WHY SAFEBOX NEEDS IT: A memory limit alone misses fast bombs and slow leaks, while
// a growth-rate window can distinguish those patterns from startup allocation.
// HOW IT WORKS: The detector evaluates only supplied samples, applies a warm-up,
// then checks caller limits, short-window spikes, and sustained long-window growth.

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "memory_sample.hpp"

namespace safebox {
namespace memory {

enum class MemoryVerdict {
    NORMAL,
    LEAK_SUSPECTED,
    BOMB_SUSPECTED,
    LIMIT_EXCEEDED
};

struct DetectorConfig {
    // Ignore growth until startup has settled for one second.
    std::chrono::milliseconds warmup_period{1000};
    // Classify a short-window increase at or above 250 MiB/s as a bomb.
    std::chrono::milliseconds bomb_window{1000};
    std::chrono::milliseconds bomb_min_window{200};
    double bomb_rate_mb_per_second{250.0};
    // Require at least three seconds of mostly positive growth for a leak verdict.
    std::chrono::milliseconds leak_window{3000};
    double leak_rate_mb_per_second{0.5};
    double leak_positive_step_fraction{0.7};
    size_t leak_min_samples{4};
};

struct DetectionResult {
    MemoryVerdict verdict{MemoryVerdict::NORMAL};
    std::string reason;
};

struct MemoryAnomalyDetector {
    // Keep detector thresholds supplied by the caller or use documented defaults.
    explicit MemoryAnomalyDetector(DetectorConfig config = DetectorConfig{});

    // Return one verdict and a measurable explanation for the supplied history.
    DetectionResult evaluate(const std::vector<MemorySample>& samples,
                             std::optional<long> limit_kb = std::nullopt) const;

    // Store configuration only; sample history remains owned by the caller.
    DetectorConfig config_;
};

// Convert a detector verdict into the stable text shown by the CLI.
const char* verdictName(MemoryVerdict verdict);

}  // namespace memory
}  // namespace safebox

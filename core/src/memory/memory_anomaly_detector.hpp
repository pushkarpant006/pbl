
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
    std::chrono::milliseconds warmup_period{1000};
    std::chrono::milliseconds bomb_window{1000};
    std::chrono::milliseconds bomb_min_window{200};
    double bomb_rate_mb_per_second{250.0};
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
    explicit MemoryAnomalyDetector(DetectorConfig config = DetectorConfig{});

    DetectionResult evaluate(const std::vector<MemorySample>& samples,
                             std::optional<long> limit_kb = std::nullopt) const;

    DetectorConfig config_;
};

const char* verdictName(MemoryVerdict verdict);

}}
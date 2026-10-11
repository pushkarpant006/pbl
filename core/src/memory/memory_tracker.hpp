// CONCEPT: Linux process memory tracking
// WHY SAFEBOX NEEDS IT: Memory decisions must be based on the kernel's measurements
// of a real process rather than estimates or simulated counters.
// HOW IT WORKS: The tracker reads status, stat, and statm under /proc, parses their
// fields, and returns one timestamped sample without applying policy.

#pragma once

#include <optional>
#include <string>

#include "memory_sample.hpp"

namespace safebox {
namespace memory {

struct ProcessStatFields {
    unsigned long minor_faults{0};
    unsigned long major_faults{0};
};

enum class SampleReadResult {
    SUCCESS,
    PROCESS_ENDED,
    ERROR
};

// Parse Linux stat fault counters while preserving process names containing parentheses.
std::optional<ProcessStatFields> parseStatLine(const std::string& line);
// Read one real process sample, distinguishing an ended process from a read error.
SampleReadResult readSample(pid_t pid, MemorySample& sample, std::string& error);

}  // namespace memory
}  // namespace safebox

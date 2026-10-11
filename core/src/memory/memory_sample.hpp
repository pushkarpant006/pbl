// CONCEPT: Process memory sample
// WHY SAFEBOX NEEDS IT: The tracker and detector need a shared representation of
// measured process memory without mixing data collection with policy decisions.
// HOW IT WORKS: A sample stores one monotonic timestamp, the observed process ID,
// memory sizes in kilobytes, and cumulative page-fault counters.

#pragma once

#include <chrono>
#include <sys/types.h>

namespace safebox {
namespace memory {

struct MemorySample {
    std::chrono::steady_clock::time_point timestamp;
    pid_t pid{0};
    long rss_kb{0};
    long vsize_kb{0};
    unsigned long minor_faults{0};
    unsigned long major_faults{0};
};

}  // namespace memory
}  // namespace safebox

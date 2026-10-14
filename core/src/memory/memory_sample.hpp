
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

}}
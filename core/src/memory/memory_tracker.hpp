
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

std::optional<ProcessStatFields> parseStatLine(const std::string& line);
SampleReadResult readSample(pid_t pid, MemorySample& sample, std::string& error);

}}
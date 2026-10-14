
#pragma once

#include <string>
#include <sys/types.h>

namespace safebox {
namespace controller {

struct LaunchedProgram {
    pid_t pid{0};
    std::string working_directory;
};

bool launchProgram(const std::string& program_path, LaunchedProgram& result, std::string& error);

}}
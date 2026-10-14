
#pragma once

#include <sys/resource.h>

namespace safebox {
namespace controller {

constexpr rlim_t PROCESS_CPU_SECONDS = 15;
constexpr rlim_t PROCESS_ADDRESS_SPACE_BYTES = 512UL * 1024UL * 1024UL;
constexpr rlim_t PROCESS_FILE_SIZE_BYTES = 10UL * 1024UL * 1024UL;
constexpr rlim_t PROCESS_OPEN_FILES = 64;
constexpr rlim_t PROCESS_CORE_BYTES = 0;

}}
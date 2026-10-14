
#pragma once

#include <string>
#include <sys/types.h>

namespace safebox {
namespace controller {

bool pauseProcess(pid_t pid, std::string& error);
bool resumeProcess(pid_t pid, std::string& error);
bool killProcess(pid_t pid, std::string& error);
bool killProcessGroup(pid_t process_group_id, std::string& error);

}}
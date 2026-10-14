
#include "process_control.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>

namespace safebox {
namespace controller {
namespace {

bool sendProcessSignal(pid_t pid, int signal_number, std::string& error) {
    if (pid <= 0 || kill(pid, signal_number) != 0) {
        error = pid <= 0 ? "PID must be positive" : std::strerror(errno);
        return false;
    }
    error.clear();
    return true;
}

}
bool pauseProcess(pid_t pid, std::string& error) {
    return sendProcessSignal(pid, SIGSTOP, error);
}

bool resumeProcess(pid_t pid, std::string& error) {
    return sendProcessSignal(pid, SIGCONT, error);
}

bool killProcess(pid_t pid, std::string& error) {
    return sendProcessSignal(pid, SIGKILL, error);
}

bool killProcessGroup(pid_t process_group_id, std::string& error) {
    if (process_group_id <= 1 || process_group_id == getpgrp()) {
        error = "process group ID is unsafe";
        return false;
    }
    if (kill(-process_group_id, SIGKILL) != 0) {
        error = std::strerror(errno);
        return false;
    }
    error.clear();
    return true;
}

}}
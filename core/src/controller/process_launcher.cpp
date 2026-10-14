
#include "process_launcher.hpp"

#include "process_limits.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace safebox {
namespace controller {
namespace {

bool createWorkingDirectory(std::string& path, std::string& error) {
    std::vector<char> template_path(32, '\0');
    const char* pattern = "/tmp/safebox-XXXXXX";
    for (int index = 0; pattern[index] != '\0'; index++) template_path[index] = pattern[index];
    if (mkdtemp(template_path.data()) == nullptr) {
        error = std::string("mkdtemp failed: ") + std::strerror(errno);
        return false;
    }
    path = template_path.data();
    return true;
}

bool setChildLimit(int resource, rlim_t limit) {
    struct rlimit setting{};
    setting.rlim_cur = limit;
    setting.rlim_max = limit;
    return setrlimit(resource, &setting) == 0;
}

bool setChildLimits() {
    return setChildLimit(RLIMIT_CPU, PROCESS_CPU_SECONDS) &&
           setChildLimit(RLIMIT_AS, PROCESS_ADDRESS_SPACE_BYTES) &&
           setChildLimit(RLIMIT_FSIZE, PROCESS_FILE_SIZE_BYTES) &&
           setChildLimit(RLIMIT_NOFILE, PROCESS_OPEN_FILES) &&
           setChildLimit(RLIMIT_CORE, PROCESS_CORE_BYTES);
}

bool redirectChildStreams() {
    int null_file = open("/dev/null", O_RDWR);
    if (null_file < 0) return false;
    bool redirected = dup2(null_file, STDIN_FILENO) >= 0 &&
                      dup2(null_file, STDOUT_FILENO) >= 0 &&
                      dup2(null_file, STDERR_FILENO) >= 0;
    close(null_file);
    return redirected;
}

void prepareChild(const std::string& path, const std::string& program_path) {
    if (setpgid(0, 0) != 0 || chdir(path.c_str()) != 0 ||
        !redirectChildStreams() || !setChildLimits()) _exit(126);
    if (raise(SIGSTOP) != 0) _exit(126);
    execl(program_path.c_str(), program_path.c_str(), static_cast<char*>(nullptr));
    _exit(127);
}

bool waitForInitialStop(pid_t pid, std::string& error) {
    int status = 0;
    pid_t result = waitpid(pid, &status, WUNTRACED);
    if (result == pid && WIFSTOPPED(status)) return true;
    error = "child failed before its initial stop";
    return false;
}

}
bool launchProgram(const std::string& program_path, LaunchedProgram& result,
                   std::string& error) {
    if (program_path.empty() || program_path[0] != '/') {
        error = "program path must be absolute";
        return false;
    }
    if (!createWorkingDirectory(result.working_directory, error)) return false;
    pid_t child_pid = fork();
    if (child_pid == -1) {
        error = std::string("fork failed: ") + std::strerror(errno);
        rmdir(result.working_directory.c_str());
        return false;
    }
    if (child_pid == 0) prepareChild(result.working_directory, program_path);
    if (!waitForInitialStop(child_pid, error)) {
        kill(child_pid, SIGKILL);
        waitpid(child_pid, nullptr, 0);
        rmdir(result.working_directory.c_str());
        return false;
    }
    result.pid = child_pid;
    return true;
}

}}
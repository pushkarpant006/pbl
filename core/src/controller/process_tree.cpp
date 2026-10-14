
#include "process_tree.hpp"

#include "proc_reader.hpp"
#include "process_control.hpp"

#include <dirent.h>
#include <cstdlib>
#include <string>
#include <vector>

namespace safebox {
namespace controller {
namespace {

bool parseDirectoryPid(const char* name, pid_t& pid) {
    if (name[0] == '\0') return false;
    for (int index = 0; name[index] != '\0'; index++) {
        if (name[index] < '0' || name[index] > '9') return false;
    }
    long value = std::strtol(name, nullptr, 10);
    if (value <= 0) return false;
    pid = static_cast<pid_t>(value);
    return true;
}

std::vector<ProcessInfo> readVisibleProcesses() {
    std::vector<ProcessInfo> processes;
    DIR* directory = opendir("/proc");
    if (directory == nullptr) return processes;
    dirent* entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        pid_t pid = 0;
        if (!parseDirectoryPid(entry->d_name, pid)) continue;
        ProcessInfo info{};
        std::string error;
        if (readProcessInfo(pid, info, error)) processes.push_back(info);
    }
    closedir(directory);
    return processes;
}

const ProcessInfo* findVisibleProcess(const std::vector<ProcessInfo>& processes, pid_t pid) {
    for (int index = 0; index < static_cast<int>(processes.size()); index++) {
        if (processes[index].pid == pid) return &processes[index];
    }
    return nullptr;
}

bool hasActiveParent(const ProcessTable& table, pid_t pid) {
    const PCB* process = table.findProcess(pid);
    return process != nullptr && process->state != ProcessState::TERMINATED;
}

bool addDescendant(ProcessTable& table, const ProcessInfo& info,
                   ProcessChanges& changes) {
    PCB process{};
    process.pid = info.pid;
    process.parent_pid = info.parent_pid;
    process.name = info.name;
    process.cpu_ms = info.cpu_ms;
    if (info.state == 'Z' || info.state == 'X') {
        process.state = ProcessState::TERMINATED;
        if (!table.addProcess(process)) {
            changes.errors.push_back("Could not record exited child " + std::to_string(info.pid));
            return false;
        }
        changes.added_pids.push_back(info.pid);
        changes.exited_pids.push_back(info.pid);
        return true;
    }
    std::string error;
    if (!pauseProcess(info.pid, error)) {
        changes.errors.push_back("Could not stop child " + std::to_string(info.pid) + ": " + error);
        return false;
    }
    process.state = ProcessState::READY;
    if (!table.addProcess(process)) {
        changes.errors.push_back("Could not record child " + std::to_string(info.pid));
        return false;
    }
    changes.added_pids.push_back(info.pid);
    return true;
}

void updateTrackedProcesses(ProcessTable& table, const std::vector<ProcessInfo>& visible,
                            ProcessChanges& changes) {
    const std::vector<PCB>& records = table.processes();
    std::vector<pid_t> known_pids;
    for (int index = 0; index < static_cast<int>(records.size()); index++) {
        known_pids.push_back(records[index].pid);
    }
    for (int index = 0; index < static_cast<int>(known_pids.size()); index++) {
        PCB* process = table.findProcess(known_pids[index]);
        if (process == nullptr || process->state == ProcessState::TERMINATED) continue;
        const ProcessInfo* info = findVisibleProcess(visible, process->pid);
        if (info != nullptr) process->cpu_ms = info->cpu_ms;
        if (info == nullptr || info->state == 'Z' || info->state == 'X') {
            table.setState(process->pid, ProcessState::TERMINATED);
            changes.exited_pids.push_back(process->pid);
        }
    }
}

void addVisibleDescendants(ProcessTable& table, pid_t root_pid,
                           const std::vector<ProcessInfo>& visible,
                           ProcessChanges& changes) {
    bool found_new = true;
    while (found_new) {
        found_new = false;
        for (int index = 0; index < static_cast<int>(visible.size()); index++) {
            const ProcessInfo& info = visible[index];
            bool has_tracked_parent = hasActiveParent(table, info.parent_pid);
            bool shares_application_group = info.process_group_id == root_pid;
            if (table.findProcess(info.pid) != nullptr ||
                (!has_tracked_parent && !shares_application_group)) {
                continue;
            }
            if (addDescendant(table, info, changes)) found_new = true;
        }
    }
}

}
ProcessTree::ProcessTree(ProcessTable& table, pid_t root_pid)
    : table_(table), root_pid_(root_pid) {
}

ProcessChanges ProcessTree::refreshTable() {
    ProcessChanges changes{};
    std::vector<ProcessInfo> visible = readVisibleProcesses();
    if (visible.empty()) {
        changes.errors.push_back("Could not read any process records from /proc.");
        return changes;
    }
    updateTrackedProcesses(table_, visible, changes);
    addVisibleDescendants(table_, root_pid_, visible, changes);
    return changes;
}

}}
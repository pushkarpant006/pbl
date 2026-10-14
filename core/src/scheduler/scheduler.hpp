
#pragma once

#include <optional>
#include <string>
#include <sys/types.h>

#include "../controller/process_table.hpp"

namespace safebox {
namespace scheduler {

class Scheduler {
public:
    virtual ~Scheduler() = default;

    virtual void addProcess(const controller::PCB& process) = 0;
    virtual std::optional<pid_t> pickNext() = 0;
    virtual long timeSliceMs(pid_t pid) = 0;
    virtual void onQuantumExpired(pid_t pid) = 0;
    virtual void onProcessFinished(pid_t pid) = 0;
    virtual bool hasReadyProcess() const = 0;
    virtual std::string name() const = 0;
};

}}
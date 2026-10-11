# SafeBox: Copilot Instructions

## Project
SafeBox is a policy-driven sandbox manager for Linux developed in WSL2 Ubuntu.
It runs untrusted programs in isolation, monitors them, detects suspicious or
unsafe behaviour, pauses a process, and lets the user allow, block, or kill it.
This is a college project for Operating Systems and DBMS, built by a team of four.

Linux enforces isolation with namespaces, cgroups, seccomp, and overlayfs. SafeBox
implements its own OS algorithms and decision logic so those concepts stay visible
in the project.

## Tech
- Core: C++17, CMake, Linux syscalls, std::thread, std::mutex, semaphores
- Allowed libraries: libseccomp, nlohmann/json, libpq or pqxx. Ask before adding others.
- Backend: Node.js, TypeScript, Express, and pg. Keep the frontend simple.
- Database: PostgreSQL schema, triggers, stored procedures, indexes, and transactions.

## Folder structure
Keep one OS concept per file.

```text
core/src/
  controller/   sandbox_controller, process_launcher, process_table (PCB), process_state
  isolation/    namespace_setup, cgroup_manager, seccomp_filter, capability_dropper
  scheduler/    scheduler interface, round_robin, priority_scheduler,
                mlfq_scheduler, scheduler_runner, scheduler_metrics
  memory/       memory_tracker, memory_anomaly_detector
  sync/         bounded_buffer, readers_writers_lock, event_pipeline
  deadlock/     bankers_algorithm, resource_manager, wait_for_graph, proc_locks_reader
  filesystem/   overlay_manager, change_inspector, canary_files
  monitor/      syscall_tracer (ptrace), event, event_collector
  policy/       policy_loader, policy_engine
  ipc/          unix_socket_server, message_protocol
  util/         logger
core/tests/     one test file per module
test_programs/  harmless programs that exercise the modules
config/policies/ strict.json, network_off.json, read_only.json
backend/        Node.js and TypeScript socket client, DB access, REST API
database/       schema.sql, triggers.sql, procedures.sql, queries.sql, seed.sql
docs/           short explanations of OS concepts and their SafeBox use
```

## Code rules
1. Code must be readable top to bottom and explainable by a student in a viva.
2. Keep one concept per file.
3. Every code file starts with a header containing:
   - `CONCEPT`: the OS concept
   - `WHY SAFEBOX NEEDS IT`: the problem solved
   - `HOW IT WORKS`: three to five plain-English lines
4. Implement real working behavior. Do not leave stubs, fake data, or TODOs.
5. Each module has a CLI demo with readable output, a test, and a harmless test program.
6. Check syscall return values and report errors. Do not silently ignore failures.
7. Shared modules communicate through clear interfaces in headers.
8. Backend REST endpoints use typed inputs and parameterized SQL. Business logic belongs
   in the core and database, not the frontend.
9. Database tables should be normalized to 3NF with keys, useful indexes, and
   transactions where multiple rows change together.

## Simple C++ and DSA style
1. Use simple structs, vectors, queues, maps, strings, if/else, for loops, while loops,
   and small functions. Use `using namespace std;` in `.cpp` files only, never headers.
2. Write explicit types such as `int`, `long`, `pid_t`, and `vector<int>`. Avoid `auto`
   except for iterators.
3. Use plain loops instead of algorithm helpers such as `transform`, `accumulate`,
   `remove_if`, or ranges.
4. Do not use lambdas, templates beyond standard containers, macros, operator
   overloading, smart pointers, or raw `new`/`delete`.
5. Prefer structs and functions. Use a class only when an interface is needed, such as
   `Scheduler`. Keep inheritance to one level.
6. Each function has one job, is at most 30 lines, and has at most three nesting levels.
   Use early returns instead of deeply nested conditions.
7. Use full, descriptive names such as `pickNextProcess`, `pauseProcess`, and
   `readMemorySample`.
8. Keep high-level logic close to pseudocode. Put raw system calls such as `kill`,
   `waitpid`, `fork`, and procfs reads in small, clearly named helper functions.
   Helpers own syscall error checks so the main flow remains clear.
9. Put one plain-English comment above every function. Explain the OS idea and why it
   matters, not the syntax.
10. Keep the file header and one-concept-per-file rule. Cleanup should be plain and
    clearly commented. Use RAII only for mounts and cgroups, with a two-line reason.

Example scheduler flow:

```cpp
// Give each ready process one time slice, then freeze it again.
void runScheduler(Scheduler& scheduler) {
    while (scheduler.hasReadyProcess()) {
        pid_t pid = scheduler.pickNext();
        resumeProcess(pid);
        waitForQuantum(pid);
        pauseProcess(pid);
        recordSlice(pid);
    }
}
```

## How to work with us
When asked to build a module:
1. First provide a short plan listing files, key functions, and the CLI demo. Wait for
   approval before implementation.
2. Then implement one file at a time in small steps.
3. After implementation, provide exact build and run commands with expected output.
4. Explain the code in plain words for a viva and list the OS concepts demonstrated.

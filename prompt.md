# SafeBox: Copilot Instructions

## Project
SafeBox is a policy-driven sandbox manager for Linux (developed in WSL2 Ubuntu).
It runs untrusted programs in isolation, monitors them, detects suspicious or
unsafe behaviour, pauses the process, and lets the user allow, block, or kill it.
This is a college project for Operating Systems + DBMS, team of 4.

Design principle: Linux ENFORCES (namespaces, cgroups, seccomp, overlayfs).
WE IMPLEMENT the OS algorithms and decision logic ourselves, so every OS concept
is visible in our own code. Never replace our own algorithm with a library call.

## Tech
- Core: C++17, CMake, Linux syscalls, std::thread, std::mutex, semaphores
- Allowed libraries: libseccomp, nlohmann/json, libpq (or pqxx). Nothing else without asking.
- Backend: Node.js + TypeScript + Express + pg (PostgreSQL). No fancy frontend.
- DB: PostgreSQL (schema, triggers, stored procedures, indexes, transactions)

## Folder structure (one folder per OS concept, one file per idea)
core/src/
  controller/   sandbox_controller, process_launcher, process_table (PCB), process_state
  isolation/    namespace_setup, cgroup_manager, seccomp_filter, capability_dropper
  scheduler/    scheduler.hpp (interface), round_robin, priority_scheduler,
                mlfq_scheduler, scheduler_runner, scheduler_metrics
  memory/       memory_tracker (/proc parsing), memory_anomaly_detector
  sync/         bounded_buffer, readers_writers_lock, event_pipeline
  deadlock/     bankers_algorithm, resource_manager, wait_for_graph, proc_locks_reader
  filesystem/   overlay_manager, change_inspector, canary_files
  monitor/      syscall_tracer (ptrace), event, event_collector
  policy/       policy_loader, policy_engine
  ipc/          unix_socket_server, message_protocol
  util/         logger
core/tests/        one test file per module
test_programs/     memory_hog, cpu_burner, file_writer, net_connect, deadlock_demo, benign_app
config/policies/   strict.json, network_off.json, read_only.json
backend/           Node.js + TypeScript: socket client, DB access, REST API
database/          schema.sql, triggers.sql, procedures.sql, queries.sql, seed.sql
docs/              one short .md per module explaining the OS concept and how we used it

## Code rules (very important)
1. READABLE FIRST. Simple C++17. No template metaprogramming, no clever tricks.
   A third-year student must understand every file in one reading.
2. ONE CONCEPT PER FILE. Opening a file must make it obvious which OS concept it
   shows (e.g. scheduler/round_robin.cpp contains only Round Robin logic).
3. EVERY FILE starts with a comment block:
   CONCEPT: which OS concept this is
   WHY SAFEBOX NEEDS IT: the real problem it solves here
   HOW IT WORKS: 3-5 plain-English lines
4. Functions are short (under 40 lines), do one thing, and have names that say
   what they do (pickNextProcess, grantIfSafe, detectCycle).
5. Comment the "why", not the "what". Explain OS ideas in plain words
   (e.g. "SIGSTOP pauses the process, which acts like preempting it").
6. REAL WORKING FEATURES ONLY. No stubs, no fake data, no "TODO: implement".
   If something cannot be done, say so and propose an alternative.
7. Every module must be runnable from a CLI demo command and print output a
   human can read (e.g. a Gantt-style trace for the scheduler, the safe sequence
   for Banker's). Output should show that it works.
8. Every module has a test in core/tests/ plus a harmless test program that
   proves it. Test programs only SIMULATE bad behaviour (allocate lots of memory,
   rewrite dummy files). Never write real malware or anything harmful.
9. Check every syscall return value. Log errors with the logger; never ignore them.
10. Use RAII for file descriptors, mounts, and cgroups so cleanup always happens.
11. Shared modules talk through clear interfaces in include/ headers, so team
    members can work in parallel without editing each other's files.
12. Backend: keep it simple and typed. REST + SQL via parameterised queries.
    Business logic lives in the core and the database, not the frontend.
13. Database: normalised to 3NF, with primary and foreign keys, indexes on common
    lookups, and transactions where several rows must change together.

## How to work with us
When asked to build a module:
1. First reply with a short PLAN: files you will create, the key functions in
   each, and the CLI demo. Wait for approval.
2. Then implement ONE file at a time, in small steps.
3. After the code, give the exact build and run commands, and the expected output.
4. Finish by explaining the code in plain words, as if we must present it in a
   viva, and list the OS concepts it demonstrates.
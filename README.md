# SafeBox

SafeBox is a Linux C++ project for exploring operating-system process scheduling
and memory monitoring. The current codebase contains two runnable demonstrations:

- A Round Robin scheduler that controls real CPU-burner child processes.
- A memory monitor that samples real processes through `/proc` and can optionally
  pause or kill a process when a configured condition is met.

These demonstrations are separate command-line programs. They are not yet wired
into one sandbox controller, and this repository does not currently implement the
full isolation, policy, backend, or database architecture described in the project
notes.

## Project flow

### Round Robin scheduling

1. `safebox_sched` reads the algorithm, quantum, process count, and CPU burst
   targets from the command line. It checks that the number of burst values
   matches `--procs`.
2. The demo forks one `cpu_burner` child per burst. Each child stops itself before
   executing the burner, and the parent confirms the stop with `waitpid`.
3. The demo creates a shared process control block (PCB) for each child and adds
   it to the Round Robin scheduler.
4. The runner picks the next ready process, resumes it with `SIGCONT`, measures
   its process CPU clock until its slice is used, then pauses it with `SIGSTOP`.
5. When a process exits, the runner confirms its status with `waitpid`. It records
   each slice and reports a Gantt-style trace and per-process timing statistics.

The runner uses real child processes; only the Round Robin algorithm unit test
uses processes as test data without running them.

### Memory monitoring

1. `safebox_mem` either launches a command with `--run` or monitors an existing
   process with `--pid`.
2. The memory tracker reads RSS, virtual memory size, and page-fault counters from
   Linux procfs files under `/proc/<pid>/`.
3. The monitor keeps a history of samples and passes it to the anomaly detector.
   The detector applies a startup warm-up, checks a caller-provided RSS limit,
   then evaluates short-window rapid growth and longer sustained growth.
4. The CLI prints memory readings, page-fault deltas, verdicts, and explanations.
   With `--action pause` or `--action kill`, the CLI applies that action for
   `LIMIT_EXCEEDED` or `BOMB_SUSPECTED`; `--action none` only reports.
5. For a launched child, the monitor checks for exit with `waitpid(WNOHANG)` and
   reaps it. If the process is paused, the monitor waits for an external decision
   before it can observe and reap a later exit.

The tracker and detector do not send signals. Process actions belong to the CLI.
The detector can also be tested using synthetic samples without procfs.

## Build and test

Requirements: Linux, a C++17 compiler, and CMake.

From the project root:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The tests cover the pure Round Robin algorithm, the real-process scheduler
runner, synthetic memory-anomaly decisions, and procfs sampling/stat-line parsing.

## Run the demonstrations

Run the scheduler demo from the build directory because it starts `./cpu_burner`
using a relative path:

```bash
cd build
./safebox_sched --algo rr --quantum 200 --procs 4 --burst 1000,2000,1500,500
```

Run the memory demos from the project root:

```bash
./build/safebox_mem --run ./build/memory_hog --target-mb 300 --interval 100 --limit-mb 200 --action pause
./build/safebox_mem --run ./build/memory_leak --interval 500
./build/safebox_mem --run ./build/benign_app --interval 200
```

The `pause` example intentionally stops the child after the memory limit is
exceeded. In another terminal, resume that child using the PID printed by the
monitor:

```bash
kill -CONT <pid>
```

Alternatively, use `--action kill` if termination is intended. The memory
workloads touch allocated pages so their resident memory can be observed by
procfs. `memory_hog` limits large requests unless `--force` is supplied.

## Important files

### Build and shared process data

| File | Purpose |
|---|---|
| `CMakeLists.txt` | Defines the scheduler and memory libraries, CLI executables, workloads, and registered tests. |
| `core/src/controller/process_table.hpp` | Defines the shared PCB and process lifecycle states used by the scheduler. It is the controller module's process-data contract. |

### Scheduler

| File | Purpose |
|---|---|
| `core/src/scheduler/scheduler.hpp` | Abstract scheduler interface shared by scheduling policies. |
| `core/src/scheduler/round_robin.hpp` and `round_robin.cpp` | Round Robin ready-queue policy and per-process scheduling state. |
| `core/src/scheduler/scheduler_demo_main.cpp` | Parses demo arguments, launches stopped CPU-burner children, and connects the scheduler to the runner. |
| `core/src/scheduler/scheduler_runner.hpp` and `scheduler_runner.cpp` | Executes real process slices using `SIGCONT`, `SIGSTOP`, process CPU clocks, and `waitpid`. |
| `core/src/scheduler/scheduler_metrics.hpp` and `scheduler_metrics.cpp` | Records slice timelines and prints timing, CPU, and context-switch summaries. |
| `test_programs/cpu_burner.cpp` | Burns a requested amount of process CPU time for the real scheduling demo. |
| `core/tests/round_robin_test.cpp` | Tests queue order and quantum behavior without running real processes. |
| `core/tests/scheduler_runner_test.cpp` | Exercises the runner with real child processes and checks execution invariants. |

### Memory monitoring

| File | Purpose |
|---|---|
| `core/src/memory/memory_sample.hpp` | Data structure for one timestamped memory and page-fault observation. |
| `core/src/memory/memory_tracker.hpp` and `memory_tracker.cpp` | Reads and parses process measurements from `/proc`; includes the pure `/proc/<pid>/stat` line parser. |
| `core/src/memory/memory_anomaly_detector.hpp` and `memory_anomaly_detector.cpp` | Applies configurable warm-up, growth-rate, and RSS-limit rules to supplied samples. |
| `core/src/memory/memory_monitor_main.cpp` | Implements the `safebox_mem` CLI, sampling loop, child reaping, output, and optional pause/kill actions. |
| `test_programs/memory_hog.cpp` | Produces rapid, touched-page memory growth for limit monitoring. |
| `test_programs/memory_leak.cpp` | Produces gradual, retained memory growth for leak detection. |
| `test_programs/benign_app.cpp` | Holds a stable touched-page allocation as a normal-behavior comparison. |
| `core/tests/memory_anomaly_detector_test.cpp` | Tests detector verdicts with synthetic sample histories. |
| `core/tests/memory_tracker_test.cpp` | Tests real procfs sampling and stat parsing, including unusual process names. |

## Configuration and safety notes

- The memory detector's thresholds are defined in `DetectorConfig` in
  `core/src/memory/memory_anomaly_detector.hpp`.
- Memory actions are opt-in and default to `none`.
- `--action pause` sends `SIGSTOP`; a paused process will not finish until it is
  resumed or otherwise handled.
- `--action kill` sends `SIGKILL` when an actionable verdict occurs.
- The memory module observes process memory; cgroup enforcement and memory limits
  are outside its responsibility.
- Scheduler and memory measurements use live Linux process data, so exact timing
  and observed RSS can vary between runs and machines.

# SafeBox

SafeBox is a local Linux process-analysis project. The Node.js website accepts a
Linux executable or script and asks the C++ analyzer to observe its process
tree, schedule real processes in user space with Round Robin turns, and report
process counts, CPU time, timeouts, and scheduler metrics. A separate CLI
implements Banker's resource-safety algorithm. PostgreSQL persistence is
available when a database is configured.

**SafeBox is not an isolation sandbox yet.** Uploaded programs run directly on
the host. The launcher applies resource limits and uses a temporary working
directory, but namespaces, cgroups, seccomp, filesystem isolation, and syscall
monitoring are not implemented. Upload only programs you trust.

## Build and run

Requirements: Linux/WSL2, C++17, CMake, Node.js 18+, npm, and PostgreSQL for
database persistence.

```bash
cmake -S . -B build
cmake --build build -j
cd backend
npm start
```

Open <http://localhost:3000>. Configure PostgreSQL with `DATABASE_URL` or
`DB_HOST`, `DB_PORT`, `DB_NAME`, `DB_USER`, and `DB_PASSWORD`. Without an
available database, analysis still runs and the result reports the persistence
error.

For a separate terminal, the analyzer can be run directly:

```bash
./build/safebox_analyze ./build/fork_tree --quantum 200 --timeout 15
./build/safebox_banker
```

The analyzer writes JSON Lines, with its final summary on the last line. The
Banker's CLI prompts for process and resource counts, per-process allocation
and declared maximum, and currently available resource counts. It checks the
supplied state; it does not infer resource needs from an uploaded program.

To set up PostgreSQL, set `DB_PASSWORD` and, when required, `DB_ADMIN_PASSWORD`,
then run `./database/setup.sh`.

## File guide

### Build and project documentation

- `CMakeLists.txt` defines the C++ libraries, executables, test programs, and
  CTest tests.
- `README.md` describes setup, limitations, and the file guide.
- `.github/copilot-instructions.md` contains repository-specific coding
  instructions for AI coding tools.
- `prompt.md` contains the original project requirements and staged plan.
- `build/` contains generated CMake files, object files, and local executables;
  regenerate it with the build commands instead of editing it as source.

### Scheduler and real process runner

- `core/src/scheduler/scheduler.hpp` defines the interface shared by scheduler
  policies.
- `core/src/scheduler/round_robin.hpp` declares the Round Robin ready queue and
  operations.
- `core/src/scheduler/round_robin.cpp` selects the next ready process and
  rotates processes after their time slices.
- `core/src/scheduler/scheduler_runner.hpp` declares the component that runs
  real child PIDs under a scheduler.
- `core/src/scheduler/scheduler_runner.cpp` resumes, pauses, samples, and
  reaps scheduler child processes.
- `core/src/scheduler/scheduler_metrics.hpp` declares slice and per-process
  scheduling measurements.
- `core/src/scheduler/scheduler_metrics.cpp` records and reports CPU slices,
  waiting time, turnaround time, response time, and context switches.
- `core/src/scheduler/scheduler_demo_main.cpp` is the standalone Round Robin CLI
  that launches CPU-burner child processes.

### Process management

- `core/src/controller/process_table.hpp` declares process records, states, and
  the shared process table.
- `core/src/controller/process_table.cpp` stores processes and checks lifecycle
  transitions.
- `core/src/controller/process_launcher.hpp` declares the program launcher.
- `core/src/controller/process_launcher.cpp` starts a program in a stopped
  process group and applies its resource limits.
- `core/src/controller/process_control.hpp` declares process signal helpers.
- `core/src/controller/process_control.cpp` resumes, pauses, and terminates
  processes and process groups.
- `core/src/controller/process_limits.hpp` sets CPU, address-space, file-size,
  open-file, and core-dump limits.
- `core/src/controller/proc_reader.hpp` declares the Linux `/proc` reader.
- `core/src/controller/proc_reader.cpp` parses process state and CPU time from
  `/proc/<pid>/stat`.
- `core/src/controller/process_tree.hpp` declares process-descendant discovery.
- `core/src/controller/process_tree.cpp` finds new and exited descendants of
  the analyzed process.

### Analyzer

- `core/src/analyzer/analyzer_main.cpp` launches an uploaded program, schedules
  its process tree, enforces the timeout, and emits live events and a summary.
- `core/src/analyzer/findings_builder.hpp` declares analyzer findings and their
  result.
- `core/src/analyzer/findings_builder.cpp` creates timeout and excessive-process
  findings and chooses the verdict.
- `core/src/analyzer/json_writer.hpp` declares JSON escaping and JSON Lines
  output.
- `core/src/analyzer/json_writer.cpp` escapes event values and flushes each
  record to standard output.

### Banker's resource-safety checker

- `core/src/deadlock/bankers_algorithm.hpp` declares the resource-state input
  and safety-check function.
- `core/src/deadlock/bankers_algorithm.cpp` validates the state and searches
  for a safe process completion sequence.
- `core/src/deadlock/bankers_main.cpp` reads a resource state from standard
  input and prints whether it is safe.

This standalone calculation needs declared maximum resource demands. It is not
a live deadlock detector and is not part of the upload analyzer.

### Memory monitor

- `core/src/memory/memory_sample.hpp` defines one timestamped memory sample.
- `core/src/memory/memory_tracker.hpp` declares process memory sampling and
  procfs parsing.
- `core/src/memory/memory_tracker.cpp` reads Linux memory counters and parses
  process status data.
- `core/src/memory/memory_anomaly_detector.hpp` declares memory growth
  thresholds and verdicts.
- `core/src/memory/memory_anomaly_detector.cpp` classifies memory growth as
  normal, a rapid-growth event, or a leak.
- `core/src/memory/memory_monitor_main.cpp` runs the standalone memory monitor
  from the command line.

The memory monitor is not currently connected to the website upload flow.

### Backend and website

- `backend/package.json` declares backend scripts, runtime, and dependencies.
- `backend/package-lock.json` locks exact npm dependency versions.
- `backend/.gitignore` excludes backend-local files such as dependencies and
  uploads.
- `backend/src/server.js` configures Express, serves the website, and mounts
  the analysis endpoint.
- `backend/src/config.js` centralizes tool paths, upload limits, and database
  connection settings.
- `backend/src/routes/analyzeRoute.js` validates uploads and options, streams
  analyzer records, persists results, and removes temporary uploads.
- `backend/src/services/fileInfoService.js` inspects and validates uploaded
  executables and scripts.
- `backend/src/services/toolRunnerService.js` starts the allowlisted analyzer,
  forwards JSON Lines, and enforces output and time limits.
- `backend/src/services/runCoordinator.js` ensures only one analysis runs at a
  time.
- `backend/src/services/databaseService.js` creates and exports the PostgreSQL
  connection pool.
- `backend/src/services/analysisPersistenceService.js` saves sessions,
  processes, actions, findings, and related records in a database transaction.
- `backend/src/services/riskService.js` maps findings to risk points and a
  threat level.
- `backend/public/index.html` contains the upload form, live process table,
  event log, and final CPU and scheduler results.

### Database

- `database/schema.sql` creates SafeBox roles, users, applications, sessions,
  process, event, policy, alert, action, report, and environment tables.
- `database/seed.sql` inserts the demo user, rule categories, and active
  process-count and timeout rules.
- `database/setup.sh` creates/configures the database role and database, applies
  the schema and seed, and creates indexes.

File, network, and syscall event tables are prepared in the schema, but their
event monitors are not implemented.

### Tests

- `core/tests/round_robin_test.cpp` checks Round Robin queue order and rotation.
- `core/tests/scheduler_runner_test.cpp` checks scheduler control of real CPU
  burner processes.
- `core/tests/memory_tracker_test.cpp` checks procfs parsing and live memory
  sampling.
- `core/tests/memory_anomaly_detector_test.cpp` checks memory detector
  classifications and thresholds.

Run the tests with `ctest --test-dir build --output-on-failure`.

### Harmless test programs

- `test_programs/benign_app.cpp` allocates a small stable memory region and
  exits normally.
- `test_programs/bounded_cpu_load.cpp` uses one CPU core for a fixed 2.5 CPU
  seconds, then exits.
- `test_programs/bounded_process_load.cpp` creates eleven short-lived children
  to test process-count findings.
- `test_programs/cpu_burner.cpp` consumes a requested CPU duration for scheduler
  tests.
- `test_programs/fork_tree.cpp` creates three short-lived child processes for
  process-tree analysis.
- `test_programs/memory_hog.cpp` allocates a bounded amount of memory for the
  standalone memory monitor.
- `test_programs/memory_leak.cpp` gradually allocates memory for the standalone
  memory monitor.

Build test programs with `cmake --build build -j`; the executables appear in
`build/` and can be selected in the website upload form.

// CONCEPT: SafeBox tool execution
// WHY SAFEBOX NEEDS IT: Backend requests must run fixed SafeBox programs with bounded resources.
// HOW IT WORKS: Spawn an allowlisted binary with argument arrays, capture bounded output, and parse JSON Lines.

const { spawn } = require("node:child_process");
const path = require("node:path");
const config = require("../config");

let toolIsRunning = false;

// Create a request error with the HTTP status used by the API.
function makeToolError(message, statusCode) {
  const error = new Error(message);
  error.statusCode = statusCode;
  return error;
}

// Convert output lines into JSON records and reject malformed tool output.
function parseJsonLines(output) {
  const records = [];
  const lines = output.split(/\r?\n/);
  for (let index = 0; index < lines.length; index += 1) {
    if (lines[index].trim() === "") continue;
    try {
      records.push(JSON.parse(lines[index]));
    } catch (error) {
      throw makeToolError(`SafeBox returned invalid JSON on output line ${index + 1}.`, 502);
    }
  }
  return records;
}

// Kill the process group so a timed-out SafeBox tool cannot leave its child running.
function killProcessGroup(child) {
  if (!child.pid) return;
  try {
    process.kill(-child.pid, "SIGKILL");
  } catch (error) {
    try {
      child.kill("SIGKILL");
    } catch (killError) {
      return;
    }
  }
}

// Release the single-run lock and reject one tool request.
function failToolRun(state, error) {
  if (state.settled) return;
  state.settled = true;
  toolIsRunning = false;
  state.reject(error);
}

// Capture only the remaining bytes allowed by the server output limit.
function captureOutput(state, chunk, streamName) {
  const remainingBytes = config.outputLimitBytes - state.outputBytes;
  const acceptedChunk = chunk.subarray(0, Math.max(0, remainingBytes));
  state.outputBytes += acceptedChunk.length;
  state[streamName] += acceptedChunk.toString("utf8");
  if (acceptedChunk.length < chunk.length) {
    state.outputExceeded = true;
    killProcessGroup(state.child);
  }
}

// Resolve a completed SafeBox process after validating its records and summary.
function finishToolRun(state, exitCode, signal) {
  clearTimeout(state.timeout);
  if (state.settled) return;
  if (state.outputExceeded) {
    failToolRun(state, makeToolError("SafeBox output exceeded the configured capture limit.", 502));
    return;
  }
  if (state.timedOut) {
    failToolRun(state, makeToolError(
      `SafeBox tool exceeded its ${state.timeoutSeconds} second timeout; its process group was terminated.`,
      504));
    return;
  }
  if (exitCode !== 0) {
    const details = state.stderr.trim() || `exit code ${exitCode}, signal ${signal || "none"}`;
    failToolRun(state, makeToolError(`SafeBox tool failed: ${details}`, 502));
    return;
  }

  try {
    const records = parseJsonLines(state.stdout);
    if (records.length === 0 || records[records.length - 1].type !== "summary") {
      throw makeToolError("SafeBox did not return a final summary record.", 502);
    }
    state.settled = true;
    toolIsRunning = false;
    state.resolve(records);
  } catch (error) {
    failToolRun(state, error);
  }
}

// Attach output, timeout, and completion handlers to one spawned tool.
function monitorTool(child, toolName, timeoutSeconds) {
  return new Promise(function waitForTool(resolve, reject) {
    const state = {
      child,
      toolName,
      timeoutSeconds,
      resolve,
      reject,
      stdout: "",
      stderr: "",
      outputBytes: 0,
      outputExceeded: false,
      timedOut: false,
      settled: false,
      timeout: null
    };

    // Capture standard output without allowing it to exceed the configured limit.
    function captureStdout(chunk) {
      captureOutput(state, chunk, "stdout");
    }

    // Capture error output within the same combined limit.
    function captureStderr(chunk) {
      captureOutput(state, chunk, "stderr");
    }

    // Report a process-start error through the normal tool failure path.
    function reportSpawnError(error) {
      clearTimeout(state.timeout);
      failToolRun(state, makeToolError(`Could not start ${toolName}: ${error.message}`, 502));
    }

    // Finish after the tool and its output streams have closed.
    function reportToolClose(exitCode, signal) {
      finishToolRun(state, exitCode, signal);
    }

    state.timeout = setTimeout(function stopTimedOutTool() {
      state.timedOut = true;
      killProcessGroup(child);
    }, timeoutSeconds * 1000);

    child.stdout.on("data", captureStdout);
    child.stderr.on("data", captureStderr);
    child.on("error", reportSpawnError);
    child.on("close", reportToolClose);
  });
}

// Spawn an allowlisted SafeBox binary with its expected working directory.
function spawnTool(toolName, argumentsList) {
  const binaryPath = path.join(config.binaryDirectory, toolName);
  const workingDirectory = toolName === "safebox_sched"
    ? config.binaryDirectory
    : config.projectRoot;
  return spawn(binaryPath, argumentsList, {
    cwd: workingDirectory,
    env: {
      PATH: "/usr/local/bin:/usr/bin:/bin",
      HOME: "/tmp",
      TMPDIR: "/tmp",
      LANG: "C",
      LC_ALL: "C"
    },
    shell: false,
    detached: true,
    stdio: ["ignore", "pipe", "pipe"]
  });
}

// Run one allowlisted SafeBox tool while enforcing the shared one-process limit.
function runTool(toolName, argumentsList, timeoutSeconds) {
  if (toolIsRunning) {
    return Promise.reject(makeToolError("Another SafeBox analysis is already running.", 429));
  }
  if (toolName !== "safebox_mem" && toolName !== "safebox_sched") {
    return Promise.reject(makeToolError("Requested SafeBox tool is not allowed.", 500));
  }

  toolIsRunning = true;
  let child;
  try {
    child = spawnTool(toolName, argumentsList);
  } catch (error) {
    toolIsRunning = false;
    return Promise.reject(makeToolError(`Could not start ${toolName}: ${error.message}`, 502));
  }
  return monitorTool(child, toolName, timeoutSeconds).catch(function releaseLock(error) {
    toolIsRunning = false;
    throw error;
  });
}

module.exports = { runTool };

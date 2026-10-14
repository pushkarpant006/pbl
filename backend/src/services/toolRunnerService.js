
const { spawn } = require("node:child_process");
const { StringDecoder } = require("node:string_decoder");
const path = require("node:path");
const config = require("../config");

let toolIsRunning = false;

function makeToolError(message, statusCode) {
  const error = new Error(message);
  error.statusCode = statusCode;
  return error;
}

function killToolGroups(child, rootPid) {
  if (rootPid > 1) {
    try {
      process.kill(-rootPid, "SIGKILL");
    } catch (error) {
      if (error.code !== "ESRCH") throw error;
    }
  }
  if (!child.pid) return;
  try {
    process.kill(-child.pid, "SIGKILL");
  } catch (error) {
    if (error.code !== "ESRCH") child.kill("SIGKILL");
  }
}

function parseAndForwardLine(state, line) {
  if (line.trim() === "") return;
  let record;
  try {
    record = JSON.parse(line);
  } catch (error) {
    throw makeToolError("SafeBox returned malformed JSON Lines output.", 502);
  }
  state.records.push(record);
  if (record.type === "process_added" && record.parent_pid === 0) {
    state.rootPid = record.pid;
  }
  state.onRecord(record);
}

function readStdoutChunk(state, chunk) {
  state.outputBytes += chunk.length;
  if (state.outputBytes > config.outputLimitBytes) {
    throw makeToolError("SafeBox output exceeded the configured capture limit.", 502);
  }
  state.pendingLine += state.decoder.write(chunk);
  let newline = state.pendingLine.indexOf("\n");
  while (newline >= 0) {
    const line = state.pendingLine.slice(0, newline).replace(/\r$/, "");
    state.pendingLine = state.pendingLine.slice(newline + 1);
    parseAndForwardLine(state, line);
    newline = state.pendingLine.indexOf("\n");
  }
}

function failStream(state, error) {
  if (state.settled) return;
  state.settled = true;
  clearTimeout(state.timeout);
  try {
    killToolGroups(state.child, state.rootPid);
  } catch (killError) {
    error.message += ` SafeBox process cleanup failed: ${killError.message}`;
  }
  state.reject(error);
}

function runToolStreaming(toolName, argumentsList, timeoutSeconds, onRecord,
                          expectedLastType = "summary", extraEnvironment = {}) {
  if (toolIsRunning) {
    return Promise.reject(makeToolError("Another SafeBox analysis is already running.", 429));
  }
  if (toolName !== "safebox_analyze") {
    return Promise.reject(makeToolError("Requested SafeBox tool is not allowed.", 500));
  }
  toolIsRunning = true;
  let child;
  try {
    child = spawn(path.join(config.binaryDirectory, toolName), argumentsList, {
      cwd: config.projectRoot,
      env: {
        PATH: "/usr/local/bin:/usr/bin:/bin",
        HOME: "/tmp",
        TMPDIR: "/tmp",
        LANG: "C",
        LC_ALL: "C",
        ...extraEnvironment
      },
      shell: false,
      detached: true,
      stdio: ["ignore", "pipe", "pipe"]
    });
  } catch (error) {
    toolIsRunning = false;
    return Promise.reject(makeToolError(`Could not start ${toolName}: ${error.message}`, 502));
  }
  return new Promise(function waitForTool(resolve, reject) {
    const state = {
      child,
      onRecord,
      expectedLastType,
      rootPid: 0,
      records: [],
      pendingLine: "",
      stderr: "",
      outputBytes: 0,
      decoder: new StringDecoder("utf8"),
      settled: false,
      timeout: null,
      resolve,
      reject
    };

    function captureStdout(chunk) {
      try {
        readStdoutChunk(state, chunk);
      } catch (error) {
        failStream(state, error);
      }
    }

    function captureStderr(chunk) {
      state.outputBytes += chunk.length;
      if (state.outputBytes > config.outputLimitBytes) {
        failStream(state, makeToolError("SafeBox output exceeded the configured capture limit.", 502));
        return;
      }
      state.stderr += chunk.toString("utf8");
    }

    function finishTool(exitCode, signal) {
      clearTimeout(state.timeout);
      if (state.settled) {
        toolIsRunning = false;
        return;
      }
      try {
        state.pendingLine += state.decoder.end();
        parseAndForwardLine(state, state.pendingLine);
        state.pendingLine = "";
        if (exitCode !== 0) {
          const details = state.stderr.trim() || `exit code ${exitCode}, signal ${signal || "none"}`;
          throw makeToolError(`SafeBox tool failed: ${details}`, 502);
        }
        if (state.expectedLastType &&
            (state.records.length === 0 ||
             state.records[state.records.length - 1].type !== state.expectedLastType)) {
          throw makeToolError(`SafeBox did not return a final ${state.expectedLastType} record.`, 502);
        }
        state.settled = true;
        resolve(state.records);
      } catch (error) {
        state.settled = true;
        reject(error);
      } finally {
        toolIsRunning = false;
      }
    }

    function reportSpawnError(error) {
      state.settled = true;
      clearTimeout(state.timeout);
      toolIsRunning = false;
      reject(makeToolError(`Could not start ${toolName}: ${error.message}`, 502));
    }

    state.timeout = setTimeout(function terminateTimedOutRun() {
      const error = makeToolError(
        `SafeBox tool exceeded its ${timeoutSeconds} second timeout.`, 504);
      failStream(state, error);
    }, timeoutSeconds * 1000);
    child.stdout.on("data", captureStdout);
    child.stderr.on("data", captureStderr);
    child.on("error", reportSpawnError);
    child.on("close", finishTool);
  });
}

module.exports = { runToolStreaming, killToolGroups };

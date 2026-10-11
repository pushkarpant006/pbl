// CONCEPT: Scheduler demonstration endpoint
// WHY SAFEBOX NEEDS IT: The page should demonstrate the real Round Robin runner, not generate a pretend timeline.
// HOW IT WORKS: Validate the quantum and CPU bursts, call safebox_sched, and return its JSON records.

const express = require("express");
const config = require("../config");
const { runTool } = require("../services/toolRunnerService");

const router = express.Router();

// Check that a supplied integer falls inside the route's supported range.
function isIntegerInRange(value, minimum, maximum) {
  return Number.isInteger(value) && value >= minimum && value <= maximum;
}

// Validate scheduler request fields and normalize an optional timeout.
function validateSchedulerRequest(body) {
  if (!body || !isIntegerInRange(body.quantumMs,
    config.minimumQuantumMs, config.maximumQuantumMs)) {
    throw new Error("quantumMs must be an integer from 50 to 1000.");
  }
  if (!Array.isArray(body.burstsMs) || body.burstsMs.length < 1 ||
      body.burstsMs.length > config.maximumProcesses) {
    throw new Error("burstsMs must contain between 1 and 8 burst values.");
  }
  for (let index = 0; index < body.burstsMs.length; index += 1) {
    if (!isIntegerInRange(body.burstsMs[index], 1, config.maximumBurstMs)) {
      throw new Error("Each CPU burst must be an integer from 1 to 5000 ms.");
    }
  }
  const timeout = body.timeoutSeconds === undefined ? 30 : body.timeoutSeconds;
  if (!isIntegerInRange(timeout, config.minimumTimeoutSeconds,
    config.maximumTimeoutSeconds)) {
    throw new Error("timeoutSeconds must be an integer from 1 to 60.");
  }
  return { quantumMs: body.quantumMs, burstsMs: body.burstsMs, timeoutSeconds: timeout };
}

// Validate that every real child finished normally before returning the summary.
function makeSchedulerSummary(record) {
  for (let index = 0; index < record.processes.length; index += 1) {
    if (record.processes[index].exit_status !== 0) {
      const processError = new Error(
        `Scheduler process ${record.processes[index].pid} did not exit normally.`
      );
      processError.statusCode = 502;
      throw processError;
    }
  }
  return {
    processes: record.processes,
    contextSwitches: record.context_switches,
    sliceCount: record.slice_count
  };
}

// Separate scheduler slice records from the final summary record.
function makeSchedulerResponse(records) {
  const slices = [];
  let summary = null;
  for (let index = 0; index < records.length; index += 1) {
    const record = records[index];
    if (record.type === "slice") {
      slices.push({
        pid: record.pid,
        startMs: record.start_ms,
        endMs: record.end_ms,
        cpuMs: record.cpu_ms
      });
    } else if (record.type === "summary") {
      summary = makeSchedulerSummary(record);
    }
  }
  if (!summary) {
    throw new Error("Scheduler did not return a final summary.");
  }
  return { slices, summary };
}

// Execute the actual Round Robin binary with only validated numeric arguments.
async function runSchedulerDemo(request, response, next) {
  try {
    const options = validateSchedulerRequest(request.body);
    const argumentsList = [
      "--algo", "rr",
      "--quantum", String(options.quantumMs),
      "--procs", String(options.burstsMs.length),
      "--burst", options.burstsMs.join(","),
      "--json"
    ];
    const records = await runTool("safebox_sched", argumentsList,
      options.timeoutSeconds);
    response.json(makeSchedulerResponse(records));
  } catch (error) {
    next(error);
  }
}

router.post("/api/scheduler-demo", runSchedulerDemo);

module.exports = router;

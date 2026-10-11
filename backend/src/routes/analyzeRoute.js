// CONCEPT: Uploaded program analysis endpoint
// WHY SAFEBOX NEEDS IT: Users need a controlled way to run a trusted upload through the real memory monitor.
// HOW IT WORKS: Validate options, inspect a private upload, invoke safebox_mem, then remove the upload.

const express = require("express");
const fs = require("node:fs");
const multer = require("multer");
const crypto = require("node:crypto");
const config = require("../config");
const { inspectFile } = require("../services/fileInfoService");
const { runTool } = require("../services/toolRunnerService");

const router = express.Router();
const upload = multer({
  storage: multer.diskStorage({
    destination: config.uploadDirectory,
    filename: function makeRandomName(request, file, callback) {
      callback(null, crypto.randomUUID());
    }
  }),
  limits: { fileSize: config.uploadLimitBytes, files: 1 }
});

// Read one integer option and enforce its inclusive limits.
function readBoundedInteger(value, name, minimum, maximum, defaultValue) {
  if (value === undefined || value === "") {
    return defaultValue;
  }
  const number = Number(value);
  if (!Number.isInteger(number) || number < minimum || number > maximum) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}.`);
  }
  return number;
}

// Validate the allowed memory action.
function readAction(value) {
  const action = value || "none";
  if (action !== "none" && action !== "pause" && action !== "kill") {
    throw new Error("action must be none, pause, or kill.");
  }
  return action;
}

// Find the final summary record returned by safebox_mem.
function findSummary(records) {
  for (let index = records.length - 1; index >= 0; index -= 1) {
    if (records[index].type === "summary") {
      return records[index];
    }
  }
  throw new Error("Memory monitor did not return a summary.");
}

// Convert one memory sample from kilobytes to the display units used by the page.
function makeSampleResponse(record) {
  return {
    elapsedMs: record.elapsed_ms,
    rssMb: record.rss_kb / 1024,
    vsizeMb: record.vsize_kb / 1024,
    minorFaultDelta: record.minor_fault_delta,
    majorFaultDelta: record.major_fault_delta,
    verdict: record.verdict,
    reason: record.reason
  };
}

// Keep sample records and exclude the final summary from the sample table.
function collectSampleResponses(records) {
  const samples = [];
  for (let index = 0; index < records.length; index += 1) {
    if (records[index].type === "sample") {
      samples.push(makeSampleResponse(records[index]));
    }
  }
  return samples;
}

// Convert memory records into the API response and display units.
function makeAnalysisResponse(fileInfo, records) {
  const summary = findSummary(records);
  return {
    file: fileInfo,
    summary: {
      verdict: summary.verdict,
      peakRssMb: summary.peak_rss_kb / 1024,
      exitStatus: summary.exit_status,
      action: summary.action,
      durationMs: summary.duration_ms
    },
    samples: collectSampleResponses(records)
  };
}

// Read and validate all memory-monitor options from the multipart request.
function readAnalysisOptions(body) {
  const interval = readBoundedInteger(body.interval, "interval",
      config.minimumIntervalMs, config.maximumIntervalMs, 100);
  const limitMb = readBoundedInteger(body.limitMb, "limit-mb",
      config.minimumLimitMb, config.maximumLimitMb, 200);
  const timeout = readBoundedInteger(body.timeout, "timeout",
      config.minimumTimeoutSeconds, config.maximumTimeoutSeconds, 30);
  const action = readAction(body.action);
  return { interval, limitMb, timeout, action };
}

// Build a fixed argument list so uploaded input cannot become a shell command.
function makeMemoryArguments(filePath, options) {
  return [
    "--run", filePath,
    "--interval", String(options.interval),
    "--limit-mb", String(options.limitMb),
    "--action", options.action,
    "--json"
  ];
}

// Inspect and execute one uploaded program with the real memory monitor.
async function runUploadedProgram(request) {
  const options = readAnalysisOptions(request.body);
  fs.chmodSync(request.file.path, 0o700);
  const fileInfo = inspectFile(request.file.path, request.file.originalname);
  const argumentsList = makeMemoryArguments(request.file.path, options);
  const records = await runTool("safebox_mem", argumentsList, options.timeout);
  return makeAnalysisResponse(fileInfo, records);
}

// Remove one temporary upload and return any cleanup problem to the route.
async function removeUploadedProgram(filePath) {
  try {
    await fs.promises.unlink(filePath);
    return null;
  } catch (error) {
    if (error.code === "ENOENT") {
      return null;
    }
    const cleanupError = new Error(`Could not remove uploaded program: ${error.message}`);
    cleanupError.statusCode = 500;
    return cleanupError;
  }
}

// Run the uploaded file and remove its temporary copy before replying.
async function analyzeProgram(request, response, next) {
  if (!request.file) {
    response.status(400).json({ error: "Choose a program file to analyze." });
    return;
  }

  let resultBody = null;
  let routeError = null;
  try {
    resultBody = await runUploadedProgram(request);
  } catch (error) {
    routeError = error;
  }
  const cleanupError = await removeUploadedProgram(request.file.path);
  if (cleanupError && routeError) {
    routeError.message += ` Also, temporary upload cleanup failed: ${cleanupError.message}`;
  } else if (cleanupError) {
    routeError = cleanupError;
  }
  if (routeError) {
    next(routeError);
    return;
  }
  response.json(resultBody);
}

router.post("/api/analyze", upload.single("file"), analyzeProgram);

module.exports = router;

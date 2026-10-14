
const express = require("express");
const fs = require("node:fs");
const multer = require("multer");
const crypto = require("node:crypto");
const path = require("node:path");
const config = require("../config");
const { inspectFile } = require("../services/fileInfoService");
const { runToolStreaming } = require("../services/toolRunnerService");
const { tryAcquireRun } = require("../services/runCoordinator");
const { persistAnalysis } = require("../services/analysisPersistenceService");
const { estimateRiskFromFindings } = require("../services/riskService");

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

function readBoundedInteger(value, name, minimum, maximum, defaultValue) {
  if (value === undefined || value === "") return defaultValue;
  const number = Number(value);
  if (!Number.isInteger(number) || number < minimum || number > maximum) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}.`);
  }
  return number;
}

function readAnalysisOptions(body) {
  return {
    quantum: readBoundedInteger(body.quantum, "quantum", config.minimumQuantumMs,
      config.maximumQuantumMs, 200),
    timeout: readBoundedInteger(body.timeout, "timeout",
      config.minimumTimeoutSeconds, config.maximumTimeoutSeconds, 15)
  };
}

function writeStreamError(response, error) {
  if (response.destroyed || response.writableEnded) return;
  response.write(`${JSON.stringify({
    type: "error",
    error: error.message || "Analysis failed."
  })}\n`);
}

async function removeUpload(filePath) {
  if (!filePath) return;
  try {
    await fs.promises.unlink(filePath);
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
}

function makeResultRecord(summary, saved, dbError, actions) {
  const risk = estimateRiskFromFindings(summary.findings || []);
  return {
    type: "result",
    verdict: summary.verdict,
    duration_ms: summary.duration_ms,
    total_cpu_ms: summary.total_cpu_ms,
    cpu_percent: summary.cpu_percent,
    process_count: summary.process_count,
    session: saved ? saved.session : null,
    processes: saved ? saved.processes : summary.processes,
    alerts: saved ? saved.alerts : [],
    actions: saved ? saved.actions : actions,
    risk_score: saved ? saved.risk_score : risk.riskScore,
    threat_level: saved ? saved.threat_level : risk.threatLevel,
    slices: summary.slices,
    scheduler_metrics: summary.scheduler_metrics,
    findings: summary.findings,
    ...(dbError ? { db_error: dbError } : {})
  };
}

async function streamAnalysis(response, programPath, fileInfo, options) {
  const actions = [];
  let summary = null;
  response.status(200);
  response.set("Content-Type", "application/x-ndjson; charset=utf-8");
  response.set("Cache-Control", "no-cache, no-transform");
  response.set("X-Content-Type-Options", "nosniff");
  response.flushHeaders();
  try {
    const records = await runToolStreaming(
      "safebox_analyze",
      [programPath, "--quantum", String(options.quantum), "--timeout", String(options.timeout)],
      options.timeout + 3,
      function forwardRecord(record) {
        if (record.type === "summary") summary = record;
        if (record.type === "action") actions.push(record);
        response.write(`${JSON.stringify(record)}\n`);
      },
      "summary",
      { SAFEBOX_PROGRAM_NAME: fileInfo.name }
    );
    if (summary === null) summary = records[records.length - 1];
    let saved = null;
    let dbError = "";
    try {
      saved = await persistAnalysis(fileInfo, summary, actions);
    } catch (error) {
      dbError = error.message;
      console.error("Could not save SafeBox analysis:", error.message);
    }
    response.write(`${JSON.stringify(makeResultRecord(summary, saved, dbError, actions))}\n`);
    response.end();
  } catch (error) {
    writeStreamError(response, error);
    response.end();
  }
}

async function analyzeUpload(request, response) {
  if (!request.file) {
    response.status(400).json({ error: "Choose a program file to analyze." });
    return;
  }
  let releaseRun = null;
  try {
    const options = readAnalysisOptions(request.body);
    fs.chmodSync(request.file.path, 0o700);
    const fileInfo = inspectFile(request.file.path, request.file.originalname);
    const extension = path.extname(fileInfo.name);
    const executablePath = `${request.file.path}${extension}`;
    if (extension) {
      fs.renameSync(request.file.path, executablePath);
      request.file.path = executablePath;
    }
    releaseRun = tryAcquireRun();
    if (releaseRun === null) {
      response.status(429).json({ error: "Another SafeBox analysis is already running." });
      return;
    }
    await streamAnalysis(response, request.file.path, {
      ...fileInfo,
      path: request.file.path
    }, options);
  } catch (error) {
    if (!response.headersSent) response.status(error.statusCode || 400).json({ error: error.message });
    else {
      writeStreamError(response, error);
      response.end();
    }
  } finally {
    if (releaseRun) releaseRun();
    try {
      await removeUpload(request.file.path);
    } catch (error) {
      console.error(error.message);
    }
  }
}

router.post("/api/analyze", upload.single("file"), analyzeUpload);

module.exports = router;

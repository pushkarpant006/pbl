
const path = require("node:path");
require("dotenv").config({ path: path.resolve(__dirname, "..", ".env") });

const express = require("express");
const fs = require("node:fs");
const multer = require("multer");
const config = require("./config");
const analyzeRoute = require("./routes/analyzeRoute");
const historyRoute = require("./routes/historyRoute");
const databasePool = require("./services/databaseService");

const app = express();

function prepareUploadDirectory() {
  fs.mkdirSync(config.uploadDirectory, { recursive: true, mode: 0o700 });
  fs.chmodSync(config.uploadDirectory, 0o700);
}

function handleError(error, request, response, next) {
  if (response.headersSent) {
    next(error);
    return;
  }
  if (error instanceof multer.MulterError) {
    const statusCode = error.code === "LIMIT_FILE_SIZE" ? 413 : 400;
    response.status(statusCode).json({ error: error.message });
    return;
  }
  const statusCode = error.statusCode || 500;
  response.status(statusCode).json({ error: error.message || "Request failed." });
}

function configureApplication() {
  prepareUploadDirectory();
  app.disable("x-powered-by");
  app.use(express.json({ limit: "32kb" }));
  app.use(express.static(config.publicDirectory));
  app.use(analyzeRoute);
  app.use(historyRoute);
  app.use(handleError);
}

// Test the configured connection so startup reports the real database status.
async function reportDatabaseConnection() {
  try {
    await databasePool.query("SELECT 1");
    console.log("SafeBox PostgreSQL database connected.");
  } catch (error) {
    console.error("SafeBox PostgreSQL database connection failed:", error.message);
    return;
  }
  try {
    const schema = await databasePool.query(
      `SELECT to_regclass('public.users') IS NOT NULL AS has_users,
              to_regclass('public.applications') IS NOT NULL AS has_applications,
              to_regclass('public.execution_sessions') IS NOT NULL AS has_sessions,
              to_regclass('public.processes') IS NOT NULL AS has_processes,
              to_regclass('public.memory_samples') IS NOT NULL AS has_memory_samples,
              to_regclass('public.file_events') IS NOT NULL AS has_file_events,
              to_regclass('public.network_events') IS NOT NULL AS has_network_events,
              to_regclass('public.syscall_events') IS NOT NULL AS has_syscall_events,
              to_regclass('public.alerts') IS NOT NULL AS has_alerts,
              to_regclass('public.actions_taken') IS NOT NULL AS has_actions,
              to_regclass('public.execution_environments') IS NOT NULL AS has_environments,
              to_regclass('public.system_info') IS NOT NULL AS has_system_info,
              to_regclass('public.threat_scores') IS NOT NULL AS has_threat_scores,
              to_regclass('public.reports') IS NOT NULL AS has_reports`
    );
    if (Object.values(schema.rows[0]).some(function missingTable(exists) {
      return !exists;
    })) {
      console.error("SafeBox database schema is incomplete; run ./database/setup.sh.");
      return;
    }
    const demoUser = await databasePool.query(
      "SELECT user_id FROM users WHERE username = $1 ORDER BY user_id LIMIT 1", ["demo"]);
    if (demoUser.rows.length === 0) {
      console.error("SafeBox database is missing the demo user; run ./database/setup.sh.");
      return;
    }
    console.log("SafeBox database schema is ready.");
  } catch (error) {
    console.error("SafeBox database readiness check failed:", error.message);
  }
}

function startServer() {
  configureApplication();
  app.listen(config.port, "127.0.0.1", function reportServerStart() {
    console.log(`SafeBox web UI is available at http://localhost:${config.port}`);
    reportDatabaseConnection();
  });
}

startServer();

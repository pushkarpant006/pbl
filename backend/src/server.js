
const express = require("express");
const fs = require("node:fs");
const multer = require("multer");
const config = require("./config");
const analyzeRoute = require("./routes/analyzeRoute");

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
  app.use(handleError);
}

function startServer() {
  configureApplication();
  app.listen(config.port, "127.0.0.1", function reportServerStart() {
    console.log(`SafeBox web UI is available at http://localhost:${config.port}`);
  });
}

startServer();
